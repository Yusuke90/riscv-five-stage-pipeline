#include <string.h>
#include "branch_predictor.h"

/* The table of selectable predictors.
 * Build with -DBP_NO_DYNAMIC until bp_dynamic.c exists. */
static const BranchPredictor *const all[] = {
    &bp_not_taken,
    &bp_btfn,
#ifndef BP_NO_DYNAMIC
    &bp_1bit,
    &bp_2bit,
#endif
};

int bp_count(void) {
    return (int)(sizeof(all) / sizeof(all[0]));
}

const BranchPredictor *bp_at(int index) {
    return (index >= 0 && index < bp_count()) ? all[index] : NULL;
}

const BranchPredictor *bp_find(const char *name) {
    for (int i = 0; i < bp_count(); i++)
        if (strcmp(all[i]->name, name) == 0)
            return all[i];
    return NULL;
}
