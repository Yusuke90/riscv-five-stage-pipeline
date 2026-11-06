#ifndef CONTROL_H
#define CONTROL_H
#include "isa_registry.h"

typedef enum {WB_NONE = 0, WB_ALU,WB_MEM, WB_PC4} WbSrc; //Writeback from where?

typedef struct{ /*this struct basically like a bitset ig the control
that is being performed is set and is returned by the control_Decode fn*/
	int reg_write; //writing to reg from mem?
	WbSrc wb_src; //whats the write back source
	int mem_read; //are we reading memory?
	int mem_write;//are we writing to memory?
	int is_branch;//are we branching?
	int is_jump;  //jump instr?
	int is_system; //identifies a system instr like an ecall(env call)
	int uses_rs1; //hazard detection tells whether the instr reads reg rs1
	int uses_rs2; //similar to above but does it read reg rs2
} ControlSignals;

ControlSignals control_decode(const DecodedInstr *i, const InstructionHandler *h);
#endif