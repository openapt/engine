/* radare - LGPL - Copyright 2012-2015 - pancake */

#include <stdio.h>
#include <string.h>
#include <r_types.h>
#include <r_lib.h>
#include <r_asm.h>

#include <v850_disas.h>

static int disassemble(RAsm *a, RAsmOp *op, const ut8 *buf, int len) {
	struct v850_cmd cmd = {
		.instr = "",
		.operands = ""
	};
	if (len < 2) return -1;
	int ret = v850_decode_command (buf, &cmd);
	if (ret > 0) {
		snprintf (op->buf_asm, R_ASM_BUFSIZE, "%s %s",
			  cmd.instr, cmd.operands);
	}
	return op->size = ret;
}

RAsmPlugin r_asm_plugin_v850 = {
	.name = "v850",
	.license = "LGPL3",
	.desc = "v850 disassembly plugin",
	.arch = "v850",
	.bits = 32,
	.init = NULL,
	.fini = NULL,
	.disassemble = &disassemble,
	.modify = NULL,
	.assemble = NULL
};

#ifndef CORELIB
struct r_lib_struct_t radare_plugin = {
	.type = R_LIB_TYPE_ASM,
	.data = &r_asm_plugin_v850,
	.version = R2_VERSION
};
#endif
