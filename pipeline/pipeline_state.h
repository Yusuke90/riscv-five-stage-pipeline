#ifndef PIPELINE_STATE_H
#define PIPELINE_STATE_H

#include <stdint.h>
#include "isa_registry.h"
#include "control.h"
#include "cpu_state.h"
#include "memory.h"
#include "branch_predictor.h"

#define PF_NONE    0 /* No fault */
#define PF_ILLEGAL 1 /* Illegal instruction */
#define PF_FETCH   2 /* Instruction fetch failed */
#define PF_MEM     3 /* Memory access failed */

/* Per-instruction event record. It travels down the pipe with the
 * instruction and is added to the global counters only when the
 * instruction retires in WB. That way a wrong-path instruction (for
 * example one fetched just past the final ecall) can never be counted. */
#define EV_NONE   0
#define EV_BRANCH 1 /* conditional branch */
#define EV_JAL    2
#define EV_JALR   3

typedef struct {
    uint8_t kind;        /* EV_* */
    uint8_t mispredict;  /* conditional branch whose guess was wrong */
    uint8_t redirected;  /* this instruction made EX redirect fetch */
    uint8_t fwd_ex;      /* operands forwarded from EX/MEM */
    uint8_t fwd_mem;     /* operands forwarded from MEM/WB */
    uint16_t stall_cyc;  /* cycles this instruction was held in ID */
    uint16_t lu_cyc;     /* ... of which load-use stalls */
} InstrEvents;

typedef struct {
    int valid;
    uint32_t pc;
    uint32_t raw;
    int fault;
    uint32_t pred_next;   /* where IF sent the fetch after this instruction */
    InstrEvents ev;
} IFID;

typedef struct {
    int valid;
    uint32_t pc;
    int fault;
    DecodedInstr instr;
    const InstructionHandler *handler;
    ControlSignals ctrl;
    uint32_t rs1_val;
    uint32_t rs2_val;
    uint32_t pred_next;   /* the guess, carried down to EX */
    InstrEvents ev;
} IDEX;

typedef struct {
    int valid;
    uint32_t pc;
    int fault;

    DecodedInstr instr;
    const InstructionHandler *handler;
    ControlSignals ctrl;

    uint32_t alu_result;
    uint32_t store_val;
    uint32_t fault_addr;
    InstrEvents ev;
} EXMEM;

typedef struct {
    int valid;
    uint32_t pc;
    int fault;

    DecodedInstr instr;
    const InstructionHandler *handler;
    ControlSignals ctrl;

    uint32_t wb_val;
    uint32_t fault_addr;
    InstrEvents ev;
} MEMWB;

typedef struct {
    IFID if_id;
    IDEX id_ex;
    EXMEM ex_mem;
    MEMWB mem_wb;

    /* configuration */
    int forwarding;               /* 0 = stall only, 1 = forwarding */
    const BranchPredictor *bp;    /* the selected predictor */
    BPState bp_state;             /* its table, owned by the pipeline */
    int trace;                    /* print one line per cycle */

    /* statistics: all except `cycles` are counted at retirement */
    uint64_t cycles, retired, stalls;
    uint64_t load_use_stalls, fwd_ex, fwd_mem;
    uint64_t branches;            /* conditional branches executed */
    uint64_t mispredicts;         /* ... whose guess was wrong */
    uint64_t jal_count;           /* redirected in IF: free */
    uint64_t jalr_count;          /* never predicted: 2 cycles each */
    uint64_t redirects;           /* mispredicts + jalr */
} Pipeline;

void pipeline_init(Pipeline *p, int forwarding, const BranchPredictor *bp);
CPUStatus pipeline_cycle(Pipeline *p, CPUState *cpu, Memory *mem);
void pipeline_run(Pipeline *p, CPUState *cpu, Memory *mem, uint64_t max_cycles);
void pipeline_print_stats(const Pipeline *p);

#endif
