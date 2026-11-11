#include "branch_predictor.h"
#include <string.h>

static int predict_not_taken(const BPState *s, uint32_t pc, uint32_t target){
    (void)s;(void)pc;(void)target;
    return 0;
}

static void update_none(BPState *s,uint32_t pc,uint32_t target,int taken){
    (void)s;(void)pc;(void)target;(void)taken;
}

static void reset_none(BPState *s){
    memset(s,0,sizeof(*s));
}

static int predict_btfn(const BPState *s, uint32_t pc, uint32_t target){
    //predicts to go back rather than front so this depends on what target and pc is
    //if target is smaller than s then predict to go back so we should return 1 else return 0
    (void)s;
    return target < pc;
}

const BranchPredictor bp_not_taken={"not-taken","branch is not taken",reset_none,predict_not_taken,update_none};
const BranchPredictor bp_btfn={"btfn","backward taken forward not taken",reset_none,predict_btfn,update_none};