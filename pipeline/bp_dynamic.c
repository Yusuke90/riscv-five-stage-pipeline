#include "branch_predictor.h"
#include <string.h>

#define CTR_WEAK_NOT_TAKEN 1
#define CTR_MAX 3

static void reset_1bit(BPState *s){
    memset(s->table,0,sizeof(s->table)); //not taken
}

static int predict_1bit(const BPState *s, uint32_t pc, uint32_t target){
    (void)target;
    return s->table[bp_index(pc)];
}

static void update_1bit(BPState *s,uint32_t pc,uint32_t target,int taken){
    (void)target;
    s->table[bp_index(pc)]= taken ? 1:0;
}

static void reset_2bit(BPState *s){
    memset(s->table,CTR_WEAK_NOT_TAKEN,sizeof(s->table));
}

static int predict_2bit(const BPState *s, uint32_t pc, uint32_t target){
    (void)target;
    return s->table[bp_index(pc)] >= 2;
}

static void update_2bit(BPState *s, uint32_t pc, uint32_t target, int taken){
    (void)target;
    uint8_t *c = &s->table[bp_index(pc)];
    if (taken) { if (*c < CTR_MAX) (*c)++; }
    else { if (*c > 0) (*c)--;} 
}

const BranchPredictor bp_1bit = {"1bit", "previous branch used in prediction" , reset_1bit, predict_1bit, update_1bit};
const BranchPredictor bp_2bit = {"2bit", "2 previous branches used in prediction", reset_2bit, predict_2bit, update_2bit};