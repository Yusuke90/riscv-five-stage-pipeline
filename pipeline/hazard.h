#include "pipeline_state.h"

typedef struct
{
    int stall;
    int load_use;
} HazardVerdict;

HazardVerdict hazard_check(const Pipeline *p, const DecodedInstr *d, const ControlSignals *c);
