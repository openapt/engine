/* radare2 - LGPL - Copyright 2013-2015 - pancake */

#include <r_asm.h>
#include <r_lib.h>
#include <capstone/capstone.h>

#define USE_ITER_API 0

static csh cd = 0;
static int n = 0;
static cs_insn *insn = NULL;

static _Bool the_end(void *p) {
#if !USE_ITER_API
	if (insn) {
		cs_free (insn, n);
		insn = NULL;
	}
#endif
	if (cd) {
		cs_close (&cd);
		cd = 0;
	}
	return true;
}

static int check_features(RAsm *a, cs_insn *insn);

static int disassemble(RAsm *a, RAsmOp *op, const ut8 *buf, int len) {
	static int omode = 0;
	int mode, ret;
	ut64 off = a->pc;

	mode = (a->bits==64)? CS_MODE_64: 
		(a->bits==32)? CS_MODE_32:
		(a->bits==16)? CS_MODE_16: 0;
	if (cd && mode != omode) {
		cs_close (&cd);
		cd = 0;
	}
	op->size = 0;
	omode = mode;
	if (cd == 0) {
		ret = cs_open (CS_ARCH_X86, mode, &cd);
		if (ret) return 0;
	}
	if (a->features && *a->features) {
		cs_option (cd, CS_OPT_DETAIL, CS_OPT_ON);
	} else {
		cs_option (cd, CS_OPT_DETAIL, CS_OPT_OFF);
	}
	if (a->syntax == R_ASM_SYNTAX_MASM) {
#if CS_OPT_SYNTAX_MASM
		cs_option (cd, CS_OPT_SYNTAX, CS_OPT_SYNTAX_MASM);
#endif
	} else if (a->syntax == R_ASM_SYNTAX_ATT) {
		cs_option (cd, CS_OPT_SYNTAX, CS_OPT_SYNTAX_ATT);
	} else {
		cs_option (cd, CS_OPT_SYNTAX, CS_OPT_SYNTAX_INTEL);
	}
	op->size = 1;
#if USE_ITER_API
	{
		size_t size = len;
		if (insn == NULL)
			insn = cs_malloc (cd);
		insn->size = 1;
		memset (insn, 0, insn->size);
		n = cs_disasm_iter (cd, (const uint8_t**)&buf, &size, (uint64_t*)&off, insn);
	}
#else
	n = cs_disasm (cd, (const ut8*)buf, len, off, 1, &insn);
#endif
	op->size = 0;
	if (a->features && *a->features) {
		if (!check_features (a, insn)) {
			op->size = insn->size;
			strcpy (op->buf_asm, "illegal");
		}
	}
	if (op->size==0 && n>0 && insn->size>0) {
		char *ptrstr;
		op->size = insn->size;
		snprintf (op->buf_asm, R_ASM_BUFSIZE, "%s%s%s",
				insn->mnemonic, insn->op_str[0]?" ":"",
				insn->op_str);
		ptrstr = strstr (op->buf_asm, "ptr ");
		if (ptrstr) {
			memmove (ptrstr, ptrstr+4, strlen (ptrstr+4)+1);
		}
	}
	if (a->syntax == R_ASM_SYNTAX_JZ) {
		if (!strncmp (op->buf_asm, "je ", 3)) {
			memcpy (op->buf_asm, "jz", 2);
		} else if (!strncmp (op->buf_asm, "jne ", 4)) {
			memcpy (op->buf_asm, "jnz", 3);
		}
	}
#if USE_ITER_API
	/* do nothing because it should be allocated once and freed in the_end */
#else
	cs_free (insn, n);
	insn = NULL;
#endif
	return op->size;
}

RAsmPlugin r_asm_plugin_x86_cs = {
	.name = "x86",
	.desc = "Capstone X86 disassembler",
	.license = "BSD",
	.arch = "x86",
	.bits = 16|32|64,
	.init = NULL,
	.fini = the_end,
	.disassemble = &disassemble,
	.assemble = NULL,
	.features = "vm,3dnow,aes,adx,avx,avx2,avx512,bmi,bmi2,cmov,"
		"f16c,fma,fma4,fsgsbase,hle,mmx,rtm,sha,sse1,sse2,"
		"sse3,sse41,sse42,sse4a,ssse3,pclmul,xop"
};

static int check_features(RAsm *a, cs_insn *insn) {
	const char *name;
	int i;
	if (!insn || !insn->detail)
		return 1;
	for (i=0; i< insn->detail->groups_count; i++) {
		int id = insn->detail->groups[i];
		if (id<128) continue;
		if (id == X86_GRP_MODE32)
			continue;
		if (id == X86_GRP_MODE64)
			continue;
		name = cs_group_name (cd, id);
		if (!name) return 1;
		if (!strstr (a->features, name)) {
			return 0;
		}
	}
	return 1;
}

#ifndef CORELIB
struct r_lib_struct_t radare_plugin = {
	.type = R_LIB_TYPE_ASM,
	.data = &r_asm_plugin_x86_cs,
	.version = R2_VERSION
};
#endif
