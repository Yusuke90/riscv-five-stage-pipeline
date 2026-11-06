#include "control.h"
#include "decoder.h"
#include "isa_registry.h"

ControlSignals control_decode(const DecodedInstr *i,
                              const InstructionHandler *h)
{
    ControlSignals ctrl = {0};

    switch (h->format)
    {
        case FMT_R:
            ctrl.reg_write = 1;
            ctrl.wb_src = WB_ALU;
            ctrl.uses_rs1 = 1;
            ctrl.uses_rs2 = 1;
            break;

        case FMT_I:
        case FMT_I_SHAMT:
            ctrl.reg_write = 1;
            ctrl.wb_src = WB_ALU;
            ctrl.uses_rs1 = 1;
            break;

        case FMT_U:
            ctrl.reg_write = 1;
            ctrl.wb_src = WB_ALU;
            break;

        case FMT_I_LOAD:
            ctrl.reg_write = 1;
            ctrl.wb_src = WB_MEM;
            ctrl.mem_read = 1;
            ctrl.uses_rs1 = 1;
            break;

        case FMT_S:
            ctrl.mem_write = 1;
            ctrl.uses_rs1 = 1;
            ctrl.uses_rs2 = 1;
            break;

        case FMT_B:
            ctrl.is_branch = 1;
            ctrl.uses_rs1 = 1;
            ctrl.uses_rs2 = 1;
            break;

        case FMT_J:
            ctrl.reg_write = 1;
            ctrl.wb_src = WB_PC4;
            ctrl.is_jump = 1;
            break;

        case FMT_I_JALR:
            ctrl.reg_write = 1;
            ctrl.wb_src = WB_PC4;
            ctrl.is_jump = 1;
            ctrl.uses_rs1 = 1;
            break;

        case FMT_I_SYS:
            ctrl.is_system = 1;
            break;

        default:
            break;
    }

    (void)i;  /* The lab's required signature includes i, but
                 this implementation only needs the instruction format. */
    return ctrl;
}