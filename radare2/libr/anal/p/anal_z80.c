/* radare - LGPL - Copyright 2012 - pancake<nopcode.org> 
				2013 - condret		*/

#include <string.h>
#include <r_types.h>
#include <r_lib.h>
#include <r_asm.h>
#include <r_anal.h>
// hack
#include "../../asm/arch/z80/z80.c"

static int z80_op(RAnal *anal, RAnalOp *op, ut64 addr, const ut8 *data, int len) {
	char out[32];
	int ilen = z80dis (0, data, out, len);

	memset (op, '\0', sizeof (RAnalOp));
	op->addr = addr;
	op->type = R_ANAL_OP_TYPE_UNK;

	switch (data[0]) {
		case 0x00:
			op->type = R_ANAL_OP_TYPE_NOP;
			break;
		case 0x03:
		case 0x04:
		case 0x0c:
		case 0x13:
		case 0x14:
		case 0x1c:
		case 0x23:
		case 0x24:
		case 0x2c:
		case 0x33:
		case 0x34:
		case 0x3c:
			op->type = R_ANAL_OP_TYPE_ADD; // INC
			break;
		case 0x09:
		case 0x19:
		case 0x29:
		case 0x39:
		case 0x80:
		case 0x81:
		case 0x82:
		case 0x83:
		case 0x84:
		case 0x85:
		case 0x86:
		case 0x87:
		case 0xc6:
			op->type = R_ANAL_OP_TYPE_ADD;
			break;
		case 0x90:
		case 0x91:
		case 0x92:
		case 0x93:
		case 0x94:
		case 0x95:
		case 0x96:
		case 0x97:
		case 0xd6:
			op->type = R_ANAL_OP_TYPE_SUB;
			break;
		case 0xc0:
		case 0xc8:
		case 0xd0:
		case 0xd8:
		case 0xe0:
		case 0xe8:
		case 0xf0:
		case 0xf8:
			op->type = R_ANAL_OP_TYPE_CRET;
			break;
		case 0xc9:
			op->type = R_ANAL_OP_TYPE_RET;
			break;
		case 0xed:
			switch(data[1]) {
				case 0x45:	//retn
				case 0x4d:	//reti
					op->type = R_ANAL_OP_TYPE_RET;
					break;
			}
			break;
		case 0x05:
		case 0x0b:
		case 0x0d:
		case 0x15:
		case 0x1b:
		case 0x1d:
		case 0x25:
		case 0x2b:
		case 0x2d:
		case 0x35:
		case 0x3b:
		case 0x3d:
			// XXXX: DEC
			op->type = R_ANAL_OP_TYPE_SUB;
			break;
		case 0xc5:
		case 0xd5:
		case 0xe5:
		case 0xf5:
			op->type = R_ANAL_OP_TYPE_PUSH;
			break;
		case 0xc1:
		case 0xd1:
		case 0xe1:
		case 0xf1:
			op->type = R_ANAL_OP_TYPE_POP;
			break;
		case 0x40:
		case 0x49:
		case 0x52:
		case 0x5b:
		case 0x64:
		case 0x6d:
		case 0x76:
		case 0x7f:
			op->type = R_ANAL_OP_TYPE_TRAP; // HALT
			break;
		case 0x10:
		case 0x18:
		case 0x20:
		case 0x28:
		case 0x30:
		case 0x38:
		case 0xc2:
		case 0xc3:
		case 0xca:
		case 0xd2:
		case 0xda:
		case 0xe2:
		case 0xe9:
		case 0xea:
		case 0xf2:
		case 0xfa:
			op->type = R_ANAL_OP_TYPE_JMP; // jmpz
			break;
		case 0xc7:				//rst 0
			op->jump = 0x00;
			op->fail = addr + ilen;
			op->type = R_ANAL_OP_TYPE_JMP;
			break;
		case 0xcf:				//rst 8
			op->jump = 0x08;
			op->fail = addr + ilen;
			op->type = R_ANAL_OP_TYPE_JMP;
			break;
		case 0xd7:				//rst 16
			op->jump = 0x10;
			op->fail = addr + ilen;
			op->type = R_ANAL_OP_TYPE_JMP;
			break;
		case 0xdf:				//rst 24
			op->jump = 0x18;
			op->fail = addr + ilen;
			op->type = R_ANAL_OP_TYPE_JMP;
			break;
		case 0xe7:				//rst 32
			op->jump = 0x20;
			op->fail = addr + ilen;
			op->type = R_ANAL_OP_TYPE_JMP;
			break;
		case 0xef:				//rst 40
			op->jump = 0x28;
			op->fail = addr + ilen;
			op->type = R_ANAL_OP_TYPE_JMP;
			break;
		case 0xf7:				//rst 48
			op->jump = 0x30;
			op->fail = addr + ilen;
			op->type = R_ANAL_OP_TYPE_JMP;
			break;
		case 0xff:				//rst 56
			op->jump = 0x38;
			op->fail = addr + ilen;
			op->type = R_ANAL_OP_TYPE_JMP;
			break;				// condret: i think that foo resets some regs, but i'm not sure

		case 0xc4:
		case 0xcc:
		case 0xcd:
		case 0xd4:
		case 0xdc:
		case 0xdd:
		case 0xe4:
		case 0xec:
		case 0xf4:
		case 0xfc:
		case 0xfd:
			op->type = R_ANAL_OP_TYPE_CALL;
			break;
		case 0xcb:			//the same as for gameboy
			switch(data[1]/8)
			{
				case 0:
				case 2:
				case 4:
				case 6:				//swap
					op->type = R_ANAL_OP_TYPE_ROL;
					break;
				case 1:
				case 3:
				case 5:
				case 7:
					op->type = R_ANAL_OP_TYPE_ROR;
					break;
				case 8:
				case 9:
				case 10:
				case 11:
				case 12:
				case 13:
				case 14:
				case 15:
					op->type = R_ANAL_OP_TYPE_AND;
					break;			//bit
				case 16:
				case 17:
				case 18:
				case 19:
				case 20:
				case 21:
				case 22:
				case 23:
					op->type = R_ANAL_OP_TYPE_XOR;
					break;			//set
				case 24:
				case 25:
				case 26:
				case 27:
				case 28:
				case 29:
				case 30:
				case 31:
					op->type = R_ANAL_OP_TYPE_MOV;
					break;			//res
			}
			break;
	}
	return op->size= ilen;
}

struct r_anal_plugin_t r_anal_plugin_z80 = {
	.name = "z80",
	.arch = R_SYS_ARCH_Z80,
	.license = "LGPL3",
	.bits = 16,
	.desc = "Z80 CPU code analysis plugin",
	.init = NULL,
	.fini = NULL,
	.op = &z80_op,
	.set_reg_profile = NULL,
	.fingerprint_bb = NULL,
	.fingerprint_fcn = NULL,
	.diff_bb = NULL,
	.diff_fcn = NULL,
	.diff_eval = NULL
};

#ifndef CORELIB
struct r_lib_struct_t radare_plugin = {
	.type = R_LIB_TYPE_ANAL,
	.data = &r_anal_plugin_z80,
	.version = R2_VERSION
};
#endif
