#include "hazard.h"
#include <stdbool.h>

int produces(int valid, const ControlSignals *lc, uint32_t rd, uint32_t rs)
{
    return valid && lc->reg_write && rd != 0 && rd == rs;
}

HazardVerdict hazard_check(const Pipeline *p, const DecodedInstr *d, const ControlSignals *c)
{
    HazardVerdict verdict = {0, 0};
    if (!p->forwarding)
    {                                            // if something stalled
        uint32_t rs1 = c->uses_rs1 ? d->rs1 : 0; // setting to 0 because our produces rejects use of x0 reg
        uint32_t rs2 = c->uses_rs2 ? d->rs2 : 0;
        bool RAW = produces(p->id_ex.valid, &p->id_ex.ctrl, p->id_ex.instr.rd, rs1) ||
                   produces(p->id_ex.valid, &p->id_ex.ctrl, p->id_ex.instr.rd, rs2) ||
                   produces(p->ex_mem.valid, &p->ex_mem.ctrl, p->ex_mem.instr.rd, rs1) ||
                   produces(p->ex_mem.valid, &p->ex_mem.ctrl, p->ex_mem.instr.rd, rs2) ||
                   produces(p->mem_wb.valid, &p->mem_wb.ctrl, p->mem_wb.instr.rd, rs1) ||
                   produces(p->mem_wb.valid, &p->mem_wb.ctrl, p->mem_wb.instr.rd, rs2);
        if (RAW)
        {
            verdict.stall = 1;
            return verdict;
        }
    }

    if (p->id_ex.valid && p->id_ex.ctrl.mem_read && p->id_ex.instr.rd != 0 &&
        ((c->uses_rs1 && d->rs1 == p->id_ex.instr.rd) ||
         (c->uses_rs2 && d->rs2 == p->id_ex.instr.rd))) {
        verdict.stall = 1;
        verdict.load_use = 1;
    }

    return verdict;
}

Operands forward_operands(const Pipeline *p,const IDEX *d){
    Operands ops={d->rs1_val,d->rs2_val,0,0};
    if(!p->forwarding || d->fault != PF_NONE) return ops;

    if(d->ctrl.uses_rs1 && d->instr.rs1!=0){
        if(produces(p->ex_mem.valid,&p->ex_mem.ctrl,p->ex_mem.instr.rd,d->instr.rs1)){
            ops.a=p->ex_mem.alu_result;
            ops.fwd_ex++;
        }
        else if(produces(p->mem_wb.valid,&p->mem_wb.ctrl,p->mem_wb.instr.rd,d->instr.rs1)){
            ops.a=p->mem_wb.wb_val;
            ops.fwd_mem++;
        }
    }
    if(d->ctrl.uses_rs2 && d->instr.rs2 != 0){    
        if(produces(p->ex_mem.valid,&p->ex_mem.ctrl,p->ex_mem.instr.rd,d->instr.rs2)){
            ops.b=p->ex_mem.alu_result;
            ops.fwd_ex++;
        }
        else if(produces(p->mem_wb.valid,&p->mem_wb.ctrl,p->mem_wb.instr.rd,d->instr.rs2)){
            ops.b=p->mem_wb.wb_val;
            ops.fwd_mem++;
        }
    }
    return ops;
}