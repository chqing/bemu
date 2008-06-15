#include "bemu.h"

beta_cpu CPU;
uint32_t *beta_mem;

#define PC_SUPERVISOR   0x80000000
#define ISR_RESET       (PC_SUPERVISOR | 0x00000000)
#define ISR_ILLOP       (PC_SUPERVISOR | 0x00000004)

inline void write_reg(beta_reg reg, uint32_t val)
{
    if(reg != 31) CPU.regs[reg] = val;
}

void bcpu_execute_one(bdecode *decode) {
    CPU.PC += 4;

    /* For now, kludgily enforce R31 is always 0*/
    CPU.regs[31] = 0;

    switch(decode->opcode) {

#define ARITH(NAME, OP)                                                 \
    case OP_ ## NAME:                                                   \
        write_reg(decode->rc,                                           \
                  CPU.regs[decode->ra] OP CPU.regs[decode->rb]);        \
    break;                                                              \
    case OP_ ## NAME ## C:                                              \
        write_reg(decode->rc,                                           \
                  CPU.regs[decode->ra] OP decode->imm);                 \
    break;
/* signed arithmetic op */
#define ARITHS(NAME, OP)                                                \
    case OP_ ## NAME:                                                   \
        write_reg(decode->rc,                                           \
                  ((int32_t)CPU.regs[decode->ra])                       \
                  OP ((int32_t)CPU.regs[decode->rb]));                  \
    break;                                                              \
    case OP_ ## NAME ## C:                                              \
        write_reg(decode->rc,                                           \
                  ((int32_t)CPU.regs[decode->ra]) OP decode->imm);      \
    break;

        ARITH(ADD, +)
        ARITH(AND, &)
        ARITH(MUL, *)
        ARITH(DIV, /)
        ARITH(OR,  |)
        ARITH(SHL, <<)
        ARITH(SHR, >>)
        ARITHS(SRA, >>)
        ARITH(SUB, -)
        ARITH(XOR, ^)
        ARITHS(CMPEQ, ==)
        ARITHS(CMPLE, <=)
        ARITHS(CMPLT, <)

#undef ARITH
#undef ARITHS

/*
 * Compute the new PC given a requested PC. Ensures that
 * JMP and friends cannot raise the privilege level by
 * setting the supervisor bit
 */
#define JMP(newpc) ((newpc) & (0x7FFFFFFF | (CPU.PC & 0x80000000)))
    case OP_JMP:
        write_reg(decode->rc, CPU.PC);
        CPU.PC = JMP(CPU.regs[decode->ra]);
        break;

    case OP_BT:
        write_reg(decode->rc, CPU.PC);
        if(CPU.regs[decode->ra]) {
            CPU.PC = JMP(CPU.PC + WORD2BYTEADDR(decode->imm));
        }
        break;

    case OP_BF:
        write_reg(decode->rc, CPU.PC);
        if(!CPU.regs[decode->ra]) {
            CPU.PC = JMP(CPU.PC + WORD2BYTEADDR(decode->imm));
        }
        break;
#undef JMP

    case OP_LD:
        LOG("LD from byte %08x", CPU.regs[decode->ra] + decode->imm);
        write_reg(decode->rc,
                  beta_mem[BYTE2WORDADDR(CPU.regs[decode->ra] + decode->imm)]);
        break;

    case OP_ST:
        LOG("ST to byte %08x", CPU.regs[decode->ra] + decode->imm);
        beta_mem[BYTE2WORDADDR(CPU.regs[decode->ra] + decode->imm)] =
            CPU.regs[decode->rc];
        break;

    case OP_LDR:
        write_reg(decode->rc,
                  beta_mem[BYTE2WORDADDR(CPU.PC + WORD2BYTEADDR(decode->imm))]);
        break;

    default:
        CPU.XP = CPU.PC;
        CPU.PC = ISR_ILLOP;
        break;
    }
}

void bcpu_reset()
{
    CPU.PC = ISR_RESET;
    /* XXX Do we need to zero registers? */
    CPU.regs[31] = 0;
}

/*
 * Advance the \Beta CPU one cycle
 */
void bcpu_step_one()
{
    bdecode decode;
    uint32_t op;

    op = beta_mem[BYTE2WORDADDR(CPU.PC & ~PC_SUPERVISOR)];
    
    decode_op(op, &decode);
    LOG("[PC=%08x bits=%08x] %s", CPU.PC, op, pp_decode(&decode));
    LOG("ra=%08x, rb=%08x, rc=%08x",
        CPU.regs[decode.ra],
        CPU.regs[decode.rb],
        CPU.regs[decode.rc]);

    bcpu_execute_one(&decode);
}
