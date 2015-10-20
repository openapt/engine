/* radare - LGPL - Copyright 2015 - danielps */

#include <r_types.h>
#include <r_util.h>

#include "v810_disas.h"

static const char *instrs[] = {
	[V810_MOV]			= "mov",
	[V810_ADD]			= "add",
	[V810_SUB]			= "sub",
	[V810_CMP]			= "cmp",
	[V810_SHL]			= "shl",
	[V810_SHR]			= "shr",
	[V810_JMP]			= "jmp",
	[V810_SAR]			= "sar",
	[V810_MUL]			= "mul",
	[V810_DIV]			= "div",
	[V810_MULU]			= "mulu",
	[V810_DIVU]			= "divu",
	[V810_OR]			= "or",
	[V810_AND]			= "and",
	[V810_XOR]			= "xor",
	[V810_NOT]			= "not",
	[V810_MOV_IMM5]		= "mov",
	[V810_ADD_IMM5]		= "add",
	[V810_CMP_IMM5]		= "cmp",
	[V810_SHL_IMM5]		= "shl",
	[V810_SHR_IMM5]		= "shr",
	[V810_SAR_IMM5]		= "sar",
	[V810_MOVEA]		= "movea",
	[V810_MOVHI]		= "movhi",
	[V810_ADDI]			= "addi",
	[V810_ORI]			= "ori",
	[V810_ANDI]			= "andi",
	[V810_XORI]			= "xori",
	[V810_JR]			= "jr",
	[V810_JAL]			= "jal",
	[V810_LDB]			= "ld.b",
	[V810_LDH]			= "ld.h",
	[V810_LDW]			= "ld.w",
	[V810_STB]			= "st.b",
	[V810_STH]			= "st.h",
	[V810_STW]			= "st.w",
	[V810_INB]			= "in.b",
	[V810_INH]			= "in.h",
	[V810_CAXI]			= "caxi",
	[V810_INW]			= "in.w",
	[V810_OUTB]			= "out.b",
	[V810_OUTH]			= "out.h",
	[V810_OUTW]			= "out.w",
	[V810_SETF]			= "setf",
	[V810_LDSR]			= "ldsr",
	[V810_STSR]			= "stsr",
	[V810_TRAP]			= "trap",
	[V810_HALT]			= "halt",
	[V810_RETI]			= "reti",
	[V810_SEI]			= "sei",
	[V810_CLI]			= "cli",
};

static const char *bit_instrs[] = {
	[V810_BIT_SCH0U]	= "sch0bsu",
	[V810_BIT_SCH0D]	= "sch0bsd",
	[V810_BIT_SCH1U]	= "sch1bsu",
	[V810_BIT_SCH1D]	= "sch1bsd",
	[V810_BIT_ORU]		= "orbsu",
	[V810_BIT_ANDU]		= "andbsu",
	[V810_BIT_XORU]		= "xorbsu",
	[V810_BIT_MOVU]		= "movbsu",
	[V810_BIT_ORNU]		= "ornbsu",
	[V810_BIT_ANDNU]	= "andnbsu",
	[V810_BIT_XORNU]	= "xornbsu",
	[V810_BIT_NOTU]		= "notbsu",
};

static const char *ext_instrs[] = {
	[V810_EXT_CMPF_S]	= "cmpf.s",
	[V810_EXT_CVT_WS]	= "cvt.ws",
	[V810_EXT_CVT_SW]	= "cvt.sw",
	[V810_EXT_ADDF_S]	= "addf.s",
	[V810_EXT_SUBF_S]	= "subf.s",
	[V810_EXT_MULF_S]	= "mulf.s",
	[V810_EXT_DIVF_S]	= "divf.s",
	[V810_EXT_XB]		= "xb",
	[V810_EXT_XH]		= "xh",
	[V810_EXT_REV]		= "rev",
	[V810_EXT_TRNC_SW]	= "trnc.sw",
	[V810_EXT_MPYHW]	= "mpyhw",
};

static const char *conds[] = {
	[V810_COND_V]	= "v",
	[V810_COND_L]	= "l",
	[V810_COND_E]	= "e",
	[V810_COND_NH]	= "nh",
	[V810_COND_N]	= "n",
	[V810_COND_R]	= "r",
	[V810_COND_LT]	= "lt",
	[V810_COND_LE]	= "le",
	[V810_COND_NV]	= "nv",
	[V810_COND_NL]	= "nl",
	[V810_COND_NE]	= "ne",
	[V810_COND_H]	= "h",
	[V810_COND_P]	= "p",
	[V810_COND_GE]	= "ge",
	[V810_COND_GT]	= "gt",
};

static const char *sysreg_names[] = {
	[V810_SREG_EIPC]	= "EIPC",
	[V810_SREG_EIPSW]	= "EIPSW",
	[V810_SREG_FEPC]	= "FEPC",
	[V810_SREG_FEPSW]	= "FEPSW",
	[V810_SREG_ECR]		= "ECR",
	[V810_SREG_PSW]		= "PSW",
	[V810_SREG_PIR]		= "PIR",
	[V810_SREG_TKCW]	= "TKCW",
	[V810_SREG_CHCW]	= "CHCW",
	[V810_SREG_ADTRE]	= "ADTRE",
};

static inline ut8 get_opcode(const ut16 instr) {
	return (instr >> 10) & 0x3F;
}

static inline ut8 get_reg1(const ut16 instr) {
	return instr & 0x1F;
}

static inline ut8 get_reg2(const ut16 instr) {
	return (instr >> 5) & 0x1F;
}

static int decode_reg_reg(const ut16 instr, struct v810_cmd *cmd) {
	ut8 opcode;

	opcode = get_opcode(instr);

	if (opcode >= sizeof(instrs) / sizeof(char *)) {
		return -1;
	}

	snprintf(cmd->instr, V810_INSTR_MAXLEN - 1, "%s", instrs[opcode]);

	if (opcode == V810_JMP) {
		snprintf(cmd->operands, V810_INSTR_MAXLEN - 1, "[r%u]",
				get_reg1(instr));
	} else {
		snprintf(cmd->operands, V810_INSTR_MAXLEN - 1, "r%u, r%u",
				get_reg1(instr), get_reg2(instr));
	}

	return 2;
}

static int decode_imm_reg(const ut16 instr, struct v810_cmd *cmd) {
	ut8 opcode;
	st8 immed;

	opcode = get_opcode(instr);

	if (opcode >= sizeof(instrs) / sizeof(char *)) {
		return -1;
	}

	snprintf(cmd->instr, V810_INSTR_MAXLEN - 1, "%s", instrs[opcode]);

	immed = get_reg1(instr);

	if (immed & 0x10) {
		immed |= 0xE0;
	}

	switch (opcode) {
		case V810_MOV_IMM5:
		case V810_ADD_IMM5:
		case V810_CMP_IMM5:
			snprintf(cmd->operands, V810_INSTR_MAXLEN - 1, "%d, r%u",
					immed, get_reg2(instr));
			break;
		case V810_LDSR:
		case V810_STSR:
			immed = immed & 0x1F;
			if (sysreg_names[(ut8)immed]) {
				snprintf(cmd->operands, V810_INSTR_MAXLEN - 1, "%s, r%u",
						sysreg_names[(ut8)immed], get_reg2(instr));
				break;
			}
		case V810_SETF:
		case V810_SHL_IMM5:
		case V810_SHR_IMM5:
		case V810_SAR_IMM5:
			snprintf(cmd->operands, V810_INSTR_MAXLEN - 1, "%u, r%u",
					immed & 0x1F, get_reg2(instr));
			break;
		case V810_TRAP:
			snprintf(cmd->operands, V810_INSTR_MAXLEN - 1, "%u",
					immed & 0x1F);
			break;
	}

	return 2;
}

static int decode_bcond(const ut16 instr, struct v810_cmd *cmd) {
	st16 disp;
	ut8 cond;

	cond = (instr >> 9) & 0xF;
	disp = instr & 0x1FE;
	if (disp & (1<<8))
		disp |= 0xFE00;

	if (cond == V810_COND_NOP) {
		snprintf(cmd->instr, V810_INSTR_MAXLEN - 1, "nop");
	} else {
		snprintf(cmd->instr, V810_INSTR_MAXLEN - 1, "b%s", conds[cond]);
		snprintf(cmd->operands, V810_INSTR_MAXLEN - 1, "%d", disp);
	}

	return 2;
}

static int decode_jump(const ut8 *instr, struct v810_cmd *cmd) {
	ut16 word1, word2;
	st32 disp;

	r_mem_copyendian((ut8*)&word1, instr, 2, LIL_ENDIAN);
	r_mem_copyendian((ut8*)&word2, instr + 2, 2, LIL_ENDIAN);

	disp = ((word1 & 0x3FF) << 16) | word2;
	if (disp & (1<<25))
		disp |= 0xFC000000;

	snprintf(cmd->instr, V810_INSTR_MAXLEN - 1, "%s",
			instrs[get_opcode(word1)]);
	snprintf(cmd->operands, V810_INSTR_MAXLEN - 1, "%d", disp);

	return 4;
}

static int decode_3operands(const ut8 *instr, struct v810_cmd *cmd) {
	ut16 word1, word2;

	r_mem_copyendian((ut8*)&word1, instr, 2, LIL_ENDIAN);
	r_mem_copyendian((ut8*)&word2, instr + 2, 2, LIL_ENDIAN);

	snprintf(cmd->instr, V810_INSTR_MAXLEN - 1, "%s",
			instrs[get_opcode(word1)]);

	if (get_opcode(word1) == V810_ADDI) {
		snprintf(cmd->operands, V810_INSTR_MAXLEN - 1, "%d, r%d, r%d",
				(st16) word2, get_reg1(word1), get_reg2(word1));
	} else {
		snprintf(cmd->operands, V810_INSTR_MAXLEN - 1, "0x%x, r%d, r%d",
				word2, get_reg1(word1), get_reg2(word1));
	}

	return 4;
}

static int decode_load_store(const ut8 *instr, struct v810_cmd *cmd) {
	ut16 word1;
	st16 word2;

	r_mem_copyendian((ut8*)&word1, instr, 2, LIL_ENDIAN);
	r_mem_copyendian((ut8*)&word2, instr + 2, 2, LIL_ENDIAN);

	snprintf(cmd->instr, V810_INSTR_MAXLEN - 1, "%s",
		instrs[get_opcode(word1)]);

	switch (get_opcode(word1)) {
		case V810_STB:
		case V810_STH:
		case V810_STW:
		case V810_OUTB:
		case V810_OUTH:
		case V810_OUTW:
			snprintf(cmd->operands, V810_INSTR_MAXLEN - 1,
					"r%d, %d[r%d]",
					get_reg2(word1), word2, get_reg1(word1));
			break;
		case V810_LDB:
		case V810_LDH:
		case V810_LDW:
		case V810_INB:
		case V810_INH:
		case V810_INW:
		case V810_CAXI:
			snprintf(cmd->operands, V810_INSTR_MAXLEN - 1,
					"%d[r%d], r%d",
					word2, get_reg1(word1), get_reg2(word1));
			break;
	}

	return 4;
}

static int decode_bit_op(const ut16 instr, struct v810_cmd *cmd) {
	ut8 subop;

	subop = get_reg1(instr);
	snprintf(cmd->instr, V810_INSTR_MAXLEN - 1, "%s", bit_instrs[subop]);

	return 2;
}

static int decode_extended(const ut8 *instr, struct v810_cmd *cmd) {
	ut8 subop;
	ut16 word1, word2;

	r_mem_copyendian((ut8*)&word1, instr, 2, LIL_ENDIAN);
	r_mem_copyendian((ut8*)&word2, instr + 2, 2, LIL_ENDIAN);

	subop = get_opcode(word2)>>2;

	snprintf(cmd->instr, V810_INSTR_MAXLEN - 1, "%s",
			ext_instrs[subop]);

	switch (subop) {
		case V810_EXT_CMPF_S:
		case V810_EXT_CVT_WS:
		case V810_EXT_CVT_SW:
		case V810_EXT_ADDF_S:
		case V810_EXT_SUBF_S:
		case V810_EXT_MULF_S:
		case V810_EXT_DIVF_S:
		case V810_EXT_REV:
		case V810_EXT_TRNC_SW:
		case V810_EXT_MPYHW:
			snprintf(cmd->operands, V810_INSTR_MAXLEN - 1, "r%d, r%d",
					get_reg1(word1), get_reg2(word1));
			break;
		case V810_EXT_XB:
		case V810_EXT_XH:
			snprintf(cmd->operands, V810_INSTR_MAXLEN - 1, "r%d",
					get_reg2(word1));
			break;
		default:
			return -1;
	}

	return 4;
}

int v810_decode_command(const ut8 *instr, struct v810_cmd *cmd) {
	int ret;
	ut16 in;

	r_mem_copyendian((ut8*)&in, instr, 2, LIL_ENDIAN);

	switch (get_opcode(in)) {
		case V810_MOV:
		case V810_ADD:
		case V810_SUB:
		case V810_CMP:
		case V810_SHL:
		case V810_SHR:
		case V810_JMP:
		case V810_SAR:
		case V810_MUL:
		case V810_DIV:
		case V810_MULU:
		case V810_DIVU:
		case V810_OR:
		case V810_AND:
		case V810_NOT:
		case V810_XOR:
			ret = decode_reg_reg(in, cmd);
			break;
		case V810_MOV_IMM5:
		case V810_ADD_IMM5:
		case V810_SETF:
		case V810_CMP_IMM5:
		case V810_SHL_IMM5:
		case V810_SHR_IMM5:
		case V810_CLI:
		case V810_SAR_IMM5:
		case V810_TRAP:
		case V810_RETI:
		case V810_HALT:
		case V810_LDSR:
		case V810_STSR:
		case V810_SEI:
			ret = decode_imm_reg(in, cmd);
			break;
		case V810_MOVEA:
		case V810_ADDI:
		case V810_ORI:
		case V810_ANDI:
		case V810_XORI:
		case V810_MOVHI:
			ret = decode_3operands(instr, cmd);
			break;
		case V810_JR:
		case V810_JAL:
			ret = decode_jump(instr, cmd);
			break;
		case V810_LDB:
		case V810_LDH:
		case V810_LDW:
		case V810_STB:
		case V810_STH:
		case V810_STW:
		case V810_INB:
		case V810_INH:
		case V810_CAXI:
		case V810_INW:
		case V810_OUTB:
		case V810_OUTH:
		case V810_OUTW:
			ret = decode_load_store(instr, cmd);
			break;
		case V810_BSTR:
			ret = decode_bit_op(in, cmd);
			break;
		case V810_EXT:
			ret = decode_extended(instr, cmd);
			break;
		default:
			if ((get_opcode(in) >> 3) == 0x4) {
				ret = decode_bcond(in, cmd);
			} else {
				ret = -1;
			}
	}

	return ret;
}
