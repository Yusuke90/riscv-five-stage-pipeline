#include <stdio.h>
#include <string.h>

#include "pipeline_state.h"
#include "control.h"
#include "cpu_state.h"
#include "decoder.h"
#include "memory.h"
#include "isa_registry.h"

/* ------------------------------------------------------------------ */
/* ALU                                                                 */
/* ------------------------------------------------------------------ */
static uint32_t alu(const DecodedInstr *i, uint32_t pc, uint32_t a, uint32_t b)
{
    uint32_t imm = (uint32_t)i->imm;

    switch (i->opcode) {

    case 0x33:   /* R-type */
    case 0x13: { /* I-type ALU */
        int is_reg   = (i->opcode == 0x33);
        uint32_t rhs = is_reg ? b : imm;
        uint32_t sh  = rhs & 0x1F;

        switch (i->funct3) {
        case 0x0:
            if (is_reg && i->funct7 == 0x20) return a - rhs;   /* sub */
            return a + rhs;                                    /* add / addi */
        case 0x1: return a << sh;                              /* sll / slli */
        case 0x2: return ((int32_t)a < (int32_t)rhs) ? 1u : 0u; /* slt / slti */
        case 0x3: return (a < rhs) ? 1u : 0u;                  /* sltu / sltiu */
        case 0x4: return a ^ rhs;                              /* xor / xori */
        case 0x5: {                                            /* srl/sra/srli/srai */
            int arith = is_reg ? (i->funct7 == 0x20)
                               : (((imm >> 10) & 1u) || i->funct7 == 0x20);
            return arith ? (uint32_t)((int32_t)a >> sh) : (a >> sh);
        }
        case 0x6: return a | rhs;                              /* or / ori */
        case 0x7: return a & rhs;                              /* and / andi */
        }
        break;
    }

    case 0x03:   /* loads: effective address */
    case 0x23:   /* stores: effective address */
        return a + imm;

    case 0x37:   /* lui */
        return imm;

    case 0x17:   /* auipc */
        return pc + imm;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Hazard detection: naive 3-latch stall                               */
/* ------------------------------------------------------------------ */
static int writes_conflict(int valid, const ControlSignals *c, unsigned rd,
                           const DecodedInstr *d, const ControlSignals *dc)
{
    if (!valid || !c->reg_write || rd == 0) return 0;
    if (dc->uses_rs1 && (unsigned)d->rs1 == rd) return 1;
    if (dc->uses_rs2 && (unsigned)d->rs2 == rd) return 1;
    return 0;
}

static int hazard(const Pipeline *p, const DecodedInstr *d, const ControlSignals *dc)
{
    return writes_conflict(p->id_ex.valid,  &p->id_ex.ctrl,  p->id_ex.instr.rd,  d, dc)
        || writes_conflict(p->ex_mem.valid, &p->ex_mem.ctrl, p->ex_mem.instr.rd, d, dc)
        || writes_conflict(p->mem_wb.valid, &p->mem_wb.ctrl, p->mem_wb.instr.rd, d, dc);
}

/* ------------------------------------------------------------------ */
/* One clock tick                                                      */
/* ------------------------------------------------------------------ */
CPUStatus pipeline_cycle(Pipeline *p, CPUState *cpu, Memory *mem)
{
    if (cpu->status != CPU_RUNNING) return cpu->status;

    Pipeline nx = *p;          /* every stage READS p, WRITES nx */
    int flush = 0;
    int stall = 0;
    uint32_t redirect_pc = 0;

    nx.cycles = p->cycles + 1;

    /* ============================ WB ============================== */
    if (p->mem_wb.valid) {
        if (p->mem_wb.fault != PF_NONE) {
            uint32_t addr;
            switch (p->mem_wb.fault) {
            case PF_ILLEGAL:
                cpu->status = CPU_ILLEGAL_INSTRUCTION;
                addr = p->mem_wb.pc;
                snprintf(cpu->fault_msg, sizeof(cpu->fault_msg),
                         "illegal instruction at pc 0x%08x", (unsigned)addr);
                break;
            case PF_FETCH:
                cpu->status = CPU_MEM_FAULT;
                addr = p->mem_wb.pc;
                snprintf(cpu->fault_msg, sizeof(cpu->fault_msg),
                         "instruction fetch fault at 0x%08x", (unsigned)addr);
                break;
            default: /* PF_MEM */
                cpu->status = CPU_MEM_FAULT;
                addr = p->mem_wb.fault_addr;
                snprintf(cpu->fault_msg, sizeof(cpu->fault_msg),
                         "memory fault at 0x%08x", (unsigned)addr);
                break;
            }
            cpu->fault_addr = addr;
            p->cycles = nx.cycles;
            return cpu->status;
        }

        if (p->mem_wb.ctrl.reg_write)
            cpu_write_reg(cpu, p->mem_wb.instr.rd, p->mem_wb.wb_val);

        if (p->mem_wb.ctrl.is_system) {
            uint32_t saved_pc = cpu->pc;
            cpu->pc = p->mem_wb.pc;
            p->mem_wb.handler->execute(cpu, mem, (DecodedInstr *)&p->mem_wb.instr);
            cpu->pc = saved_pc;
        }

        nx.retired = p->retired + 1;

        /* ecall exit: stop now so younger instructions do nothing more */
        if (cpu->status != CPU_RUNNING) {
            *p = nx;
            return cpu->status;
        }
    }

    /* ============================ MEM ============================= */
    nx.mem_wb.valid      = p->ex_mem.valid;
    nx.mem_wb.pc         = p->ex_mem.pc;
    nx.mem_wb.fault      = p->ex_mem.fault;
    nx.mem_wb.fault_addr = p->ex_mem.fault_addr;
    nx.mem_wb.instr      = p->ex_mem.instr;
    nx.mem_wb.handler    = p->ex_mem.handler;
    nx.mem_wb.ctrl       = p->ex_mem.ctrl;
    nx.mem_wb.wb_val     = p->ex_mem.alu_result;

    if (p->ex_mem.valid && p->ex_mem.fault == PF_NONE) {
        MemStatus ms = MEM_OK;
        uint32_t addr = p->ex_mem.alu_result;

        if (p->ex_mem.ctrl.mem_read) {
            uint32_t value = 0;
            switch (p->ex_mem.instr.funct3) {
            case 0x0: { uint8_t b;
                ms = memory_read_byte(mem, addr, &b);
                if (ms == MEM_OK) value = (uint32_t)(int32_t)(int8_t)b;
                break; }
            case 0x1: { uint16_t h;
                ms = memory_read_half(mem, addr, &h);
                if (ms == MEM_OK) value = (uint32_t)(int32_t)(int16_t)h;
                break; }
            case 0x2:
                ms = memory_read_word(mem, addr, &value);
                break;
            case 0x4: { uint8_t b;
                ms = memory_read_byte(mem, addr, &b);
                if (ms == MEM_OK) value = b;
                break; }
            case 0x5: { uint16_t h;
                ms = memory_read_half(mem, addr, &h);
                if (ms == MEM_OK) value = h;
                break; }
            default:
                ms = MEM_ERR_OUT_OF_BOUNDS;
                break;
            }
            if (ms == MEM_OK) nx.mem_wb.wb_val = value;
        }
        else if (p->ex_mem.ctrl.mem_write) {
            switch (p->ex_mem.instr.funct3) {
            case 0x0: ms = memory_write_byte(mem, addr, (uint8_t)p->ex_mem.store_val);  break;
            case 0x1: ms = memory_write_half(mem, addr, (uint16_t)p->ex_mem.store_val); break;
            case 0x2: ms = memory_write_word(mem, addr, p->ex_mem.store_val);           break;
            default:  ms = MEM_ERR_OUT_OF_BOUNDS; break;
            }
        }

        if (ms != MEM_OK) {
            nx.mem_wb.fault      = PF_MEM;
            nx.mem_wb.fault_addr = addr;
        }
    }

    /* ============================ EX ============================== */
    nx.ex_mem.valid      = p->id_ex.valid;
    nx.ex_mem.pc         = p->id_ex.pc;
    nx.ex_mem.fault      = p->id_ex.fault;
    nx.ex_mem.fault_addr = 0;
    nx.ex_mem.instr      = p->id_ex.instr;
    nx.ex_mem.handler    = p->id_ex.handler;
    nx.ex_mem.ctrl       = p->id_ex.ctrl;
    nx.ex_mem.store_val  = p->id_ex.rs2_val;
    nx.ex_mem.alu_result = 0;

    if (p->id_ex.valid && p->id_ex.fault == PF_NONE) {
        const DecodedInstr *d = &p->id_ex.instr;
        uint32_t a = p->id_ex.rs1_val;
        uint32_t b = p->id_ex.rs2_val;
        uint32_t pc = p->id_ex.pc;

        if (p->id_ex.ctrl.is_branch) {
            int taken = 0;
            switch (d->funct3) {
            case 0x0: taken = (a == b); break;
            case 0x1: taken = (a != b); break;
            case 0x4: taken = ((int32_t)a <  (int32_t)b); break;
            case 0x5: taken = ((int32_t)a >= (int32_t)b); break;
            case 0x6: taken = (a <  b); break;
            case 0x7: taken = (a >= b); break;
            }
            if (taken) {
                flush = 1;
                redirect_pc = pc + (uint32_t)d->imm;
            }
        }
        else if (p->id_ex.ctrl.is_jump) {
            nx.ex_mem.alu_result = pc + 4;
            flush = 1;
            if (d->opcode == 0x6F)       /* jal */
                redirect_pc = pc + (uint32_t)d->imm;
            else                         /* jalr */
                redirect_pc = (a + (uint32_t)d->imm) & ~1u;
        }
        else {
            nx.ex_mem.alu_result = alu(d, pc, a, b);
        }
    }

    /* ============================ ID ============================== */
    nx.id_ex.valid = 0;   /* default: bubble */

    if (p->if_id.valid) {
        if (p->if_id.fault != PF_NONE) {
            /* carry the fetch fault down the pipe */
            memset(&nx.id_ex.instr, 0, sizeof(nx.id_ex.instr));
            memset(&nx.id_ex.ctrl,  0, sizeof(nx.id_ex.ctrl));
            nx.id_ex.valid   = 1;
            nx.id_ex.pc      = p->if_id.pc;
            nx.id_ex.fault   = p->if_id.fault;
            nx.id_ex.handler = NULL;
        }
        else {
            DecodedInstr d;
            const InstructionHandler *h = NULL;

            if (decode_instruction(p->if_id.raw, &d, &h) == DECODE_UNKNOWN_INSTRUCTION) {
                memset(&nx.id_ex.instr, 0, sizeof(nx.id_ex.instr));
                memset(&nx.id_ex.ctrl,  0, sizeof(nx.id_ex.ctrl));
                nx.id_ex.valid   = 1;
                nx.id_ex.pc      = p->if_id.pc;
                nx.id_ex.fault   = PF_ILLEGAL;
                nx.id_ex.handler = NULL;
            }
            else {
                ControlSignals ctrl = control_decode(&d, h);

                if (hazard(p, &d, &ctrl)) {
                    stall = 1;                    /* bubble goes into ID/EX */
                }
                else {
                    nx.id_ex.valid   = 1;
                    nx.id_ex.pc      = p->if_id.pc;
                    nx.id_ex.fault   = PF_NONE;
                    nx.id_ex.instr   = d;
                    nx.id_ex.handler = h;
                    nx.id_ex.ctrl    = ctrl;
                    nx.id_ex.rs1_val = cpu_read_reg(cpu, d.rs1);
                    nx.id_ex.rs2_val = cpu_read_reg(cpu, d.rs2);
                }
            }
        }
    }

    /* A taken branch/jump kills the wrong-path instruction now in ID.
       Flush wins over a stall in the same cycle. */
    if (flush) {
        nx.id_ex.valid = 0;
        stall = 0;
        nx.flushes = p->flushes + 1;
    }
    else if (stall) {
        nx.stalls = p->stalls + 1;
    }

    /* ============================ IF ============================== */
    if (stall) {
        /* freeze: IF/ID and the fetch PC both hold */
        nx.if_id = p->if_id;
    }
    else {
        uint32_t fetch_pc = flush ? redirect_pc : cpu->pc;
        uint32_t raw = 0;
        MemStatus s = memory_read_word(mem, fetch_pc, &raw);

        nx.if_id.valid = 1;
        nx.if_id.pc    = fetch_pc;
        if (s != MEM_OK) {
            nx.if_id.raw   = 0;
            nx.if_id.fault = PF_FETCH;
        } else {
            nx.if_id.raw   = raw;
            nx.if_id.fault = PF_NONE;
        }
        cpu->pc = fetch_pc + 4;
    }

#ifdef PIPE_TRACE
    fprintf(stderr, "c%-4llu IF:%08x ID:%c EX:%c MEM:%c WB:%c %s%s\n",
            (unsigned long long)nx.cycles, (unsigned)nx.if_id.pc,
            p->if_id.valid  ? '*' : '.',
            p->id_ex.valid  ? '*' : '.',
            p->ex_mem.valid ? '*' : '.',
            p->mem_wb.valid ? '*' : '.',
            stall ? "STALL " : "", flush ? "FLUSH" : "");
#endif

    *p = nx;   /* commit: one clock edge */
    return cpu->status;
}

void pipeline_init(Pipeline *p) {
    memset(p, 0, sizeof(*p));
}

void pipeline_run(Pipeline *p, CPUState *cpu, Memory *mem, uint64_t max_cycles) {
    while (cpu->status == CPU_RUNNING && p->cycles < max_cycles)
        pipeline_cycle(p, cpu, mem);
}