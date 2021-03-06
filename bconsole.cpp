#include "bemu.h"

#include <termios.h>
#include <errno.h>
#include <pthread.h>
#include <poll.h>
#include <signal.h>

static pthread_cond_t console_cond;
static pthread_mutex_t console_mutex;
static pthread_t console_thread;

static struct termios saved_termios;
static int kbd_char;
extern beta_cpu CPU;

class LockMutex {
private:
    pthread_mutex_t &mutex;
public:
    LockMutex(pthread_mutex_t &m) : mutex(m) {
        pthread_mutex_lock(&mutex);
    }
    ~LockMutex() {
        pthread_mutex_unlock(&mutex);
    }
};

void* console_process(void *arg UNUSED) {
    int err;
    struct pollfd pollfd = {};
    pollfd.fd   = 0;
    pollfd.events = POLLIN;

    while(1) {
        err = poll(&pollfd, 1, -1);
        if(err < 0) {
            if(errno == EINTR) continue;
            panic("Poll returned error: %s\n", strerror(errno));
        }
        LOG("Keyboard interrupt!\n");
        {
            LockMutex locked(console_mutex);
            err = read(0, &kbd_char, 1);

            if(err == 0)
                kbd_char = -1;

            CPU.set_interrupt(INT_KBD);

            pthread_cond_wait(&console_cond, &console_mutex);
        }

        if(err == 0)
            break;
    }
    return NULL;
}

void console_open(bool interrupt) {
    LOG("console_open(%d)", interrupt);
    struct termios termios;

    /* Disable echo */
    if(tcgetattr(0, &saved_termios) == 0) {
        termios = saved_termios;
        termios.c_lflag &= ~(ECHO|ICANON);
        if(tcsetattr(0, TCSANOW, &termios) < 0) {
            panic("Can't tcsettattr(0): %s", strerror(errno));
        }
    }

    signal(SIGIO, SIG_IGN);

    atexit(console_close);

    setvbuf(stdout, NULL, _IONBF, 0);

    if(interrupt) {
        LOG("Creating keyboard processing thread...");
        if(pthread_mutex_init(&console_mutex, NULL) < 0) {
            panic("Can't create console mutex: %s", strerror(errno));
        }
        if(pthread_cond_init(&console_cond, NULL) < 0) {
            panic("Can't create console condition var: %s", strerror(errno));
        }
        if(pthread_create(&console_thread, NULL,
                          console_process, NULL) < 0) {
            panic("Can't pthread_create: %s", strerror(errno));
        }
    }
}

void console_close() {
    tcsetattr(0, TCSANOW, &saved_termios);
}

void beta_wrchr(int chr) {
    putchar(chr);
}

int beta_rdchr() {
    int c;

    {
        LockMutex locked(console_mutex);
        c = kbd_char;
        kbd_char = 0;
        CPU.clear_interrupt(INT_KBD);
        pthread_cond_signal(&console_cond);
    }
    return c;
}

