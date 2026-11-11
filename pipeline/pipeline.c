#include <stdio.h>
#include <string.h>

#include "pipeline_state.h"
#include "control.h"
#include "cpu_state.h"
#include "decoder.h"
#include "memory.h"
#include "isa_registry.h"
#include "hazard.h"
#include "branch_predictor.h"

/* ------------------------------------------------------------------ */
/* ALU                                                                 */
/* ------------------------------------------------------------------ */
static uint32_t alu(const DecodedInstr *i, uint32_t pc, uint32_t a, uint32_t b)
{
    uint32_t imm = (uint32_t)i->imm;

    switch (i->opcode)
    {
    case 0x33: /* R-type */
    case 0x13: /* I-type ALU */
    {
        int is_reg = (i->opcode == 0x33);
        uint32_t rhs = is_reg ? b : imm;
        uint32_t sh = rhs & 0x1F;

        switch (i->funct3)
        {
        case 0x0:
            if (is_reg && i->funct7 == 0x20)
                return a - rhs; /* sub */
            return a + rhs;     /* add / addi */
        case 0x1:
            return a << sh; /* sll / slli */
        case 0x2:
            return ((int32_t)a < (int32_t)rhs) ? 1u : 0u; /* slt / slti */
        case 0x3:
            return (a < rhs) ? 1u : 0u; /* sltu / sltiu */
        case 0x4:
            return a ^ rhs; /* xor / xori */
        case 0x5:
        { /* srl/sra/srli/srai */
            int arith = is_reg ? (i->funct7 == 0x20)
                               : (((imm >> 10) & 1u) || i->funct7 == 0x20);
            return arith ? (uint32_t)((int32_t)a >> sh) : (a >> sh);
        }
        case 0x6:
            return a | rhs; /* or / ori */
        case 0x7:
            return a & rhs; /* and / andi */
        }
        break;
    }

    case 0x03: /* loads: effective address */
    case 0x23: /* stores: effective address */
        return a + imm;

    case 0x37: /* lui */
        return imm;

    case 0x17: /* auipc */
        return pc + imm;
    }
    return 0;
}

/* Print one pipeline-stage slot for --trace. */
static void trace_slot(int valid, uint32_t pc)
{
    if (valid)
        printf("0x%08x", (unsigned)pc);
    else
        printf("----------");
}

/* ------------------------------------------------------------------ */
/* One clock tick                                                      */
/* ------------------------------------------------------------------ */
CPUStatus pipeline_cycle(Pipeline *p, CPUState *cpu, Memory *mem)
{
    if (cpu->status != CPU_RUNNING)
        return cpu->status;

    Pipeline nx = *p; /* every stage READS p, WRITES nx */
    int flush = 0;    /* EX found the guess wrong (or hit a jalr) */
    int stall = 0;    /* ID is held this cycle */
    int load_use = 0;
    uint32_t redirect_pc = 0;
    int fetched = 0;          /* for --trace */
    uint32_t fetched_pc = 0;

    nx.cycles = p->cycles + 1;

    /* ============================ WB ============================== */
    if (p->mem_wb.valid)
    {
        if (p->mem_wb.fault != PF_NONE)
        {
            uint32_t addr;
            switch (p->mem_wb.fault)
            {
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

        if (p->mem_wb.ctrl.is_system)
        {
            uint32_t saved_pc = cpu->pc;
            cpu->pc = p->mem_wb.pc;
            p->mem_wb.handler->execute(cpu, mem, (DecodedInstr *)&p->mem_wb.instr);
            cpu->pc = saved_pc;
        }

        nx.retired = p->retired + 1;

        /* fold this instruction's events into the global counters */
        {
            const InstrEvents *e = &p->mem_wb.ev;
            nx.stalls = p->stalls + e->stall_cyc;
            nx.load_use_stalls = p->load_use_stalls + e->lu_cyc;
            nx.fwd_ex = p->fwd_ex + e->fwd_ex;
            nx.fwd_mem = p->fwd_mem + e->fwd_mem;
            nx.redirects = p->redirects + (e->redirected ? 1u : 0u);
            if (e->kind == EV_BRANCH)
            {
                nx.branches = p->branches + 1;
                nx.mispredicts = p->mispredicts + (e->mispredict ? 1u : 0u);
            }
            else if (e->kind == EV_JAL)
                nx.jal_count = p->jal_count + 1;
            else if (e->kind == EV_JALR)
                nx.jalr_count = p->jalr_count + 1;
        }

        /* ecall exit: stop now so younger instructions do nothing more */
        if (cpu->status != CPU_RUNNING)
        {
            *p = nx;
            return cpu->status;
        }
    }

    /* ============================ MEM ============================= */
    nx.mem_wb.valid = p->ex_mem.valid;
    nx.mem_wb.pc = p->ex_mem.pc;
    nx.mem_wb.fault = p->ex_mem.fault;
    nx.mem_wb.fault_addr = p->ex_mem.fault_addr;
    nx.mem_wb.instr = p->ex_mem.instr;
    nx.mem_wb.handler = p->ex_mem.handler;
    nx.mem_wb.ctrl = p->ex_mem.ctrl;
    nx.mem_wb.wb_val = p->ex_mem.alu_result;
    nx.mem_wb.ev = p->ex_mem.ev;

    if (p->ex_mem.valid && p->ex_mem.fault == PF_NONE)
    {
        MemStatus ms = MEM_OK;
        uint32_t addr = p->ex_mem.alu_result;

        if (p->ex_mem.ctrl.mem_read)
        {
            uint32_t value = 0;
            switch (p->ex_mem.instr.funct3)
            {
            case 0x0:
            {
                uint8_t b;
                ms = memory_read_byte(mem, addr, &b);
                if (ms == MEM_OK)
                    value = (uint32_t)(int32_t)(int8_t)b;
                break;
            }
            case 0x1:
            {
                uint16_t h;
                ms = memory_read_half(mem, addr, &h);
                if (ms == MEM_OK)
                    value = (uint32_t)(int32_t)(int16_t)h;
                break;
            }
            case 0x2:
                ms = memory_read_word(mem, addr, &value);
                break;
            case 0x4:
            {
                uint8_t b;
                ms = memory_read_byte(mem, addr, &b);
                if (ms == MEM_OK)
                    value = b;
                break;
            }
            case 0x5:
            {
                uint16_t h;
                ms = memory_read_half(mem, addr, &h);
                if (ms == MEM_OK)
                    value = h;
                break;
            }
            default:
                ms = MEM_ERR_OUT_OF_BOUNDS;
                break;
            }
            if (ms == MEM_OK)
                nx.mem_wb.wb_val = value;
        }
        else if (p->ex_mem.ctrl.mem_write)
        {
            switch (p->ex_mem.instr.funct3)
            {
            case 0x0:
                ms = memory_write_byte(mem, addr, (uint8_t)p->ex_mem.store_val);
                break;
            case 0x1:
                ms = memory_write_half(mem, addr, (uint16_t)p->ex_mem.store_val);
                break;
            case 0x2:
                ms = memory_write_word(mem, addr, p->ex_mem.store_val);
                break;
            default:
                ms = MEM_ERR_OUT_OF_BOUNDS;
                break;
            }
        }

        if (ms != MEM_OK)
        {
            nx.mem_wb.fault = PF_MEM;
            nx.mem_wb.fault_addr = addr;
        }
    }

    /* ============================ EX ============================== */
    nx.ex_mem.valid = p->id_ex.valid;
    nx.ex_mem.pc = p->id_ex.pc;
    nx.ex_mem.fault = p->id_ex.fault;
    nx.ex_mem.fault_addr = 0;
    nx.ex_mem.instr = p->id_ex.instr;
    nx.ex_mem.handler = p->id_ex.handler;
    nx.ex_mem.ctrl = p->id_ex.ctrl;
    nx.ex_mem.alu_result = 0;
    nx.ex_mem.store_val = p->id_ex.rs2_val;
    nx.ex_mem.ev = p->id_ex.ev;

    if (p->id_ex.valid && p->id_ex.fault == PF_NONE)
    {
        Operands o = forward_operands(p, &p->id_ex);
        nx.ex_mem.ev.fwd_ex = (uint8_t)o.fwd_ex;
        nx.ex_mem.ev.fwd_mem = (uint8_t)o.fwd_mem;

        const DecodedInstr *d = &p->id_ex.instr;
        uint32_t a = o.a;
        uint32_t b = o.b;
        uint32_t pc = p->id_ex.pc;
        nx.ex_mem.store_val = o.b;

        if (p->id_ex.ctrl.is_branch)
        {
            int taken = 0;
            switch (d->funct3)
            {
            case 0x0:
                taken = (a == b);
                break;
            case 0x1:
                taken = (a != b);
                break;
            case 0x4:
                taken = ((int32_t)a < (int32_t)b);
                break;
            case 0x5:
                taken = ((int32_t)a >= (int32_t)b);
                break;
            case 0x6:
                taken = (a < b);
                break;
            case 0x7:
                taken = (a >= b);
                break;
            }
            uint32_t target = pc + (uint32_t)d->imm;
            uint32_t real_next = taken ? target : pc + 4;

            /* teach the predictor the real outcome */
            if (p->bp)
                p->bp->update(&nx.bp_state, pc, target, taken);

            nx.ex_mem.ev.kind = EV_BRANCH;
            if (real_next != p->id_ex.pred_next)
            {
                nx.ex_mem.ev.mispredict = 1;
                nx.ex_mem.ev.redirected = 1;
                flush = 1;
                redirect_pc = real_next;
            }
        }
        else if (p->id_ex.ctrl.is_jump)
        {
            nx.ex_mem.alu_result = pc + 4; /* link value */
            uint32_t real_next;
            if (d->opcode == 0x6F) /* jal: IF already redirected */
            {
                real_next = pc + (uint32_t)d->imm;
                nx.ex_mem.ev.kind = EV_JAL;
            }
            else /* jalr: never predicted */
            {
                real_next = (a + (uint32_t)d->imm) & ~1u;
                nx.ex_mem.ev.kind = EV_JALR;
            }
            if (real_next != p->id_ex.pred_next)
            {
                nx.ex_mem.ev.redirected = 1;
                flush = 1;
                redirect_pc = real_next;
            }
        }
        else
        {
            nx.ex_mem.alu_result = alu(d, pc, a, b);
        }
    }

    /* ============================ ID ============================== */
    nx.id_ex.valid = 0; /* default: bubble */
    nx.id_ex.pred_next = p->if_id.pred_next;
    nx.id_ex.ev = p->if_id.ev;

    if (p->if_id.valid)
    {
        if (p->if_id.fault != PF_NONE)
        {
            /* carry the fetch fault down the pipe */
            memset(&nx.id_ex.instr, 0, sizeof(nx.id_ex.instr));
            memset(&nx.id_ex.ctrl, 0, sizeof(nx.id_ex.ctrl));
            nx.id_ex.valid = 1;
            nx.id_ex.pc = p->if_id.pc;
            nx.id_ex.fault = p->if_id.fault;
            nx.id_ex.handler = NULL;
        }
        else
        {
            DecodedInstr d;
            const InstructionHandler *h = NULL;

            if (decode_instruction(p->if_id.raw, &d, &h) == DECODE_UNKNOWN_INSTRUCTION)
            {
                memset(&nx.id_ex.instr, 0, sizeof(nx.id_ex.instr));
                memset(&nx.id_ex.ctrl, 0, sizeof(nx.id_ex.ctrl));
                nx.id_ex.valid = 1;
                nx.id_ex.pc = p->if_id.pc;
                nx.id_ex.fault = PF_ILLEGAL;
                nx.id_ex.handler = NULL;
            }
            else
            {
                ControlSignals ctrl = control_decode(&d, h);
                HazardVerdict hz = hazard_check(p, &d, &ctrl);

                if (hz.stall)
                {
                    stall = 1; /* bubble goes into ID/EX */
                    load_use = hz.load_use;
                }
                else
                {
                    nx.id_ex.valid = 1;
                    nx.id_ex.pc = p->if_id.pc;
                    nx.id_ex.fault = PF_NONE;
                    nx.id_ex.instr = d;
                    nx.id_ex.handler = h;
                    nx.id_ex.ctrl = ctrl;
                    nx.id_ex.rs1_val = cpu_read_reg(cpu, d.rs1);
                    nx.id_ex.rs2_val = cpu_read_reg(cpu, d.rs2);
                }
            }
        }
    }

    /* A redirect kills the wrong-path instruction now in ID. A redirect
       wins over a stall in the same cycle. */
    if (flush)
    {
        nx.id_ex.valid = 0;
        stall = 0;
    }

    /* ============================ IF ============================== */
    if (stall)
    {
        /* freeze: IF/ID and the fetch PC both hold. The stall is charged
           to the held instruction and counted when it retires. */
        nx.if_id = p->if_id;
        nx.if_id.ev.stall_cyc = (uint16_t)(p->if_id.ev.stall_cyc + 1);
        if (load_use)
            nx.if_id.ev.lu_cyc = (uint16_t)(p->if_id.ev.lu_cyc + 1);
    }
    else
    {
        uint32_t fetch_pc = cpu->pc;
        uint32_t raw = 0;
        uint32_t next = fetch_pc + 4;
        MemStatus s = memory_read_word(mem, fetch_pc, &raw);

        fetched = 1;
        fetched_pc = fetch_pc;
        nx.if_id.valid = 1;
        nx.if_id.pc = fetch_pc;
        memset(&nx.if_id.ev, 0, sizeof(nx.if_id.ev));
        if (s != MEM_OK)
        {
            nx.if_id.raw = 0;
            nx.if_id.fault = PF_FETCH;
        }
        else
        {
            nx.if_id.raw = raw;
            nx.if_id.fault = PF_NONE;

            /* pre-decode: conditional branch -> ask the predictor,
               jal -> redirect now, anything else -> pc + 4 */
            DecodedInstr fd;
            const InstructionHandler *fh = NULL;
            if (decode_instruction(raw, &fd, &fh) != DECODE_UNKNOWN_INSTRUCTION)
            {
                if (fh->format == FMT_B)
                {
                    uint32_t target = fetch_pc + (uint32_t)fd.imm;
                    if (p->bp && p->bp->predict(&p->bp_state, fetch_pc, target))
                        next = target;
                }
                else if (fh->format == FMT_J)
                {
                    next = fetch_pc + (uint32_t)fd.imm;
                }
            }
        }
        nx.if_id.pred_next = next;
        cpu->pc = next;
    }

    /* A redirect also squashes what IF just fetched and restarts fetch
       at the real next PC: two instructions thrown away in total. */
    if (flush)
    {
        nx.if_id.valid = 0;
        cpu->pc = redirect_pc;
    }

    if (p->trace)
    {
        printf("[cycle %llu] IF ", (unsigned long long)nx.cycles);
        trace_slot(fetched, fetched_pc);
        printf(" | ID ");
        trace_slot(p->if_id.valid, p->if_id.pc);
        printf(" | EX ");
        trace_slot(p->id_ex.valid, p->id_ex.pc);
        printf(" | MEM ");
        trace_slot(p->ex_mem.valid, p->ex_mem.pc);
        printf(" | WB ");
        trace_slot(p->mem_wb.valid, p->mem_wb.pc);
        if (stall)
            printf("  STALL%s", load_use ? " (load-use)" : "");
        if (flush)
            printf("  REDIRECT -> 0x%08x", (unsigned)redirect_pc);
        printf("\n");
    }

    *p = nx; /* commit: one clock edge */
    return cpu->status;
}

void pipeline_init(Pipeline *p, int forwarding, const BranchPredictor *bp)
{
    memset(p, 0, sizeof(*p));
    p->forwarding = forwarding;
    p->bp = bp;
    if (bp)
        bp->reset(&p->bp_state);
}

void pipeline_run(Pipeline *p, CPUState *cpu, Memory *mem, uint64_t max_cycles)
{
    while (cpu->status == CPU_RUNNING && p->cycles < max_cycles)
        pipeline_cycle(p, cpu, mem);
}

void pipeline_print_stats(const Pipeline *p)
{
    printf("pipeline    = 5-stage, hazard unit '%s', predictor '%s'\n",
           p->forwarding ? "forward" : "stall",
           p->bp ? p->bp->name : "none");
    printf("cycles      = %llu\n", (unsigned long long)p->cycles);
    printf("retired     = %llu\n", (unsigned long long)p->retired);
    printf("CPI         = %.3f\n",
           p->retired ? (double)p->cycles / (double)p->retired : 0.0);
    printf("stalls      = %llu (of which load-use: %llu)\n",
           (unsigned long long)p->stalls, (unsigned long long)p->load_use_stalls);
    printf("forwards    = %llu (EX/MEM: %llu, MEM/WB: %llu)\n",
           (unsigned long long)(p->fwd_ex + p->fwd_mem),
           (unsigned long long)p->fwd_ex, (unsigned long long)p->fwd_mem);
    printf("branches    = %llu conditional branches executed\n",
           (unsigned long long)p->branches);
    printf("mispredicts = %llu ... of which the guess was wrong\n",
           (unsigned long long)p->mispredicts);
    if (p->branches)
        printf("accuracy    = %.1f%%\n",
               100.0 * (double)(p->branches - p->mispredicts) / (double)p->branches);
    else
        printf("accuracy    = n/a (no conditional branches)\n");
    printf("jal         = %llu redirected in IF: free\n", (unsigned long long)p->jal_count);
    printf("jalr        = %llu never predicted: 2 cycles each\n", (unsigned long long)p->jalr_count);
    printf("redirects   = %llu mispredicts + jalr\n", (unsigned long long)p->redirects);
    printf("squashed    = %llu instructions thrown away (2 per redirect)\n",
           (unsigned long long)(2 * p->redirects));
}