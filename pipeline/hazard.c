#include "hazard.h"

static int produces(int valid,const ControlSignals *lc,uint32_t rd,uint32_t rs){
    return valid && lc->reg_write && rd!=0 && rd==rs;
} //one needs to send the correct registers themselves
/* is the instr valid and are we writing if we are writing 
are we writing to reg x0 and are the regs the same rd is from 
curr instr and rs is from next instr*/
HazardVerdict hazard_check(const Pipeline *p,const DecodedInstr *d, const ControlSignals *c){
    HazardVerdict verdict ={0,0};
    if(!p->forwarding){ //if something stalled
        uint32_t rs1=c->uses_rs1 ? d->rs1 : 0; //setting to 0 because our produces rejects use of x0 reg
        uint32_t rs2=c->uses_rs2 ? d->rs2 : 0;
        bool RAW=produces(p->id_ex.valid,&p->id_ex.ctrl,p->id_ex.instr.rd,rs1)||
                 produces(p->id_ex.valid,&p->id_ex.ctrl,p->id_ex.instr.rd,rs2)||
                 produces(p->ex_mem.valid,p->ex_mem.ctrl,p->ex_mem.instr.rd,rs1)||
                 produces(p->ex_mem.valid,&p->ex_mem.ctrl,p->ex_mem.instr.rd,rs2)||
                 produces(p->mem_wb.valid,&p->mem_wb.ctrl,p->mem_wb.instr.rd,rs1)||
                 produces(p->mem_wb.valid,&p->mem_wb.ctrl,p->mem_wb.instr.rd,rs2);
        if(RAW){
                v.stall=1;
                return v;
            }

        return v;
    }
}