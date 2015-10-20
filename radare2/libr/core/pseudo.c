/* radare - LGPL - Copyright 2015 - pancake */

#include <r_core.h>

R_API int r_core_pseudo_code (RCore *core, const char *input) {
	Sdb *db;
	RAnalFunction *fcn = r_anal_get_fcn_in (core->anal,
		core->offset, R_ANAL_FCN_TYPE_NULL);
	int asmpseudo = r_config_get_i (core->config, "asm.pseudo");
	int asmdecode = r_config_get_i (core->config, "asm.decode");
	int asmlines = r_config_get_i (core->config, "asm.lines");
	int asmbytes = r_config_get_i (core->config, "asm.bytes");
	int asmoffset = r_config_get_i (core->config, "asm.offset");
	int asmflags = r_config_get_i (core->config, "asm.flags");
	int asmfcnlines = r_config_get_i (core->config, "asm.fcnlines");
	int asmcomments = r_config_get_i (core->config, "asm.comments");
	int asmfunctions = r_config_get_i (core->config, "asm.functions");
	if (!fcn) {
		eprintf ("Cannot find function in 0x%08"PFMT64x"\n",
			core->offset);
		return false;
	}
	r_config_set_i (core->config, "asm.pseudo", 1);
	r_config_set_i (core->config, "asm.decode", 0);
	r_config_set_i (core->config, "asm.lines", 0);
	r_config_set_i (core->config, "asm.bytes", 0);
	r_config_set_i (core->config, "asm.offset", 0);
	r_config_set_i (core->config, "asm.flags", 0);
	r_config_set_i (core->config, "asm.fcnlines", 0);
	r_config_set_i (core->config, "asm.comments", 0);
	r_config_set_i (core->config, "asm.functions", 0);

	db = sdb_new0 ();

	/* */
	// walk all basic blocks
	// define depth level for each block
	// use it for indentation
	// asm.pseudo=true
	// asm.decode=true
	RAnalBlock *bb = r_list_first (fcn->bbs);
	char indentstr[1024];
	int n_bb = r_list_length (fcn->bbs);
	r_cons_printf ("function %s () {", fcn->name);
	int indent = 1;
	int nindent = 1;

	do {
#define I_TAB 4
#define K_ELSE(x) sdb_fmt(0,"else.%"PFMT64x,x)
#define K_INDENT(x) sdb_fmt(0,"loc.%"PFMT64x,x)
#define SET_INDENT(x) { memset (indentstr, ' ', x*I_TAB); indentstr [(x*I_TAB)-2] = 0; }
		if (!bb) break;
		r_cons_push ();
		char *code = r_core_cmd_str (core, sdb_fmt(0,
			"pDI %d @ 0x%08"PFMT64x"\n", bb->size, bb->addr));
		r_cons_pop ();
		memset (indentstr, ' ', indent*I_TAB);
		indentstr [(indent*I_TAB)-2] = 0;
		code = r_str_prefix_all (code, indentstr);
		code[strlen(code)-1] = 0; // chop last newline
		//r_cons_printf ("\n%s  loc_0x%llx:\n", indentstr, bb->addr);
		//if (nindent != indent) {
		//	r_cons_printf ("\n%s  loc_0x%llx:\n", indentstr, bb->addr);
		//}
			r_cons_printf ("\n%s  loc_0x%llx:\n", indentstr, bb->addr);
		indentstr[(indent*I_TAB)-2] = 0;
		r_cons_printf ("\n%s", code);
		free (code);
		if (sdb_get (db, K_INDENT(bb->addr), 0)) {
			// already analyzed, go pop and continue
			// XXX check if cant pop
			//eprintf ("%s// 0x%08llx already analyzed\n", indentstr, bb->addr);
			ut64 addr = sdb_array_pop_num (db, "indent", NULL);
			if (addr==UT64_MAX) {
				int i;
				nindent = 1;
				for (i=indent; i!=nindent; i--) {
					SET_INDENT (i);
					r_cons_printf ("\n%s}", indentstr);
				}
				r_cons_printf ("\n%sreturn;\n", indentstr);
				break;
			}
			if (sdb_num_get (db, K_ELSE(bb->addr), 0)) {
				r_cons_printf ("\n%s} else {", indentstr);
			} else {
				r_cons_printf ("\n%s}", indentstr);
			}
			r_cons_printf ("\n%s  goto loc_0x%llx", indentstr, addr);
			bb = r_anal_bb_from_offset (core->anal, addr);
			if (!bb) {
				eprintf ("failed block\n");
				break;
			}
			//eprintf ("next is %llx\n", addr);
			nindent = sdb_num_get (db, K_INDENT(addr), NULL);
			if (indent>nindent) {
				int i;
				for (i=indent; i!=nindent; i--) {
					SET_INDENT (i);
					r_cons_printf ("\n%s}", indentstr);
				}
			}
			indent = nindent;
		} else {
			sdb_set (db, K_INDENT(bb->addr), "passed", 0);
			if (bb->jump != UT64_MAX) {
				int swap = 1;
				// TODO: determine which branch take first
				ut64 jump = swap? bb->jump: bb->fail;
				ut64 fail = swap? bb->fail: bb->jump;
				// if its from another function chop it!
				RAnalFunction *curfcn = r_anal_get_fcn_in (core->anal, jump, R_ANAL_FCN_TYPE_NULL);
				if (curfcn != fcn) {
					// chop that branch
					r_cons_printf ("\n  // chop\n");
					break;
				}
				if (sdb_get (db, K_INDENT(jump), 0)) {
					// already tracekd
					if (!sdb_get (db, K_INDENT(fail), 0)) {
						bb = r_anal_bb_from_offset (core->anal, fail);
					}
				} else {
					bb = r_anal_bb_from_offset (core->anal, jump);
					if (!bb) {
						eprintf ("failed to retrieve blcok at 0x%"PFMT64x"\n", jump);
						break;
					}
					if (fail != UT64_MAX) {
						// do not push if already pushed
						indent++;
						if (sdb_get (db, K_INDENT(bb->fail), 0)) {
							/* do nothing here */
							eprintf ("BlockAlready 0x%"PFMT64x"\n", bb->addr);
						} else {
					//		r_cons_printf (" { RADICAL %llx\n", bb->addr);
							sdb_array_push_num (db, "indent", fail, 0);
							sdb_num_set (db, K_INDENT(fail), indent, 0);
							sdb_num_set (db, K_ELSE(fail), 1, 0);
							r_cons_printf (" {");
						}
					} else {
						r_cons_printf (" do");
						sdb_array_push_num (db, "indent", jump, 0);
						sdb_num_set (db, K_INDENT(jump), indent, 0);
						sdb_num_set (db, K_ELSE(jump), 1, 0);
						r_cons_printf (" {");
						indent++;
					}
				}
			} else {
				ut64 addr = sdb_array_pop_num (db, "indent", NULL);
				if (addr==UT64_MAX) {
					r_cons_printf ("\nbreak\n");
					break;
				}
				bb = r_anal_bb_from_offset (core->anal, addr);
				nindent = sdb_num_get (db, K_INDENT(addr), NULL);
				if (indent>nindent) {
					int i;
					for (i=indent; i!=nindent; i--) {
						SET_INDENT (i);
						r_cons_printf ("\n%s}", indentstr);
					}
				}
				if (nindent != indent) {
					r_cons_printf ("\n%s} else {\n", indentstr);
				}
				indent = nindent;
			}
		}
		//n_bb --;
	} while (n_bb>0);
	r_cons_printf ("}\n");
	r_config_set_i (core->config, "asm.pseudo", asmpseudo);
	r_config_set_i (core->config, "asm.decode", asmdecode);
	r_config_set_i (core->config, "asm.lines", asmlines);
	r_config_set_i (core->config, "asm.bytes", asmbytes);
	r_config_set_i (core->config, "asm.offset", asmoffset);
	r_config_set_i (core->config, "asm.flags", asmflags);
	r_config_set_i (core->config, "asm.fcnlines", asmfcnlines);
	r_config_set_i (core->config, "asm.comments", asmcomments);
	r_config_set_i (core->config, "asm.functions", asmfunctions);
	sdb_free (db);
	return true;
}
