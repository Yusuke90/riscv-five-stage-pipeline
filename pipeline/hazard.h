#ifndef HAZARD_H
#define HAZARD_H

#include "pipeline_state.h"
#include <stdint.h>
typedef struct
{
    int stall;
    int load_use;
} HazardVerdict;

typedef struct
{
    uint32_t a, b;
    int fwd_ex, fwd_mem;
} Operands;

HazardVerdict hazard_check(const Pipeline *p, const DecodedInstr *d, const ControlSignals *c);
Operands forward_operands(const Pipeline *p, const IDEX *d);
int produces(int valid, const ControlSignals *lc, uint32_t rd, uint32_t rs); // one needs to send the correct registers themselves
/* is the instr valid and are we writing if we are writing
are we writing to reg x0 and are the regs the same rd is from
curr instr and rs is from next instr*/
#endif 