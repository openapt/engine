/* radare - LGPL - Copyright 2011-2015 - earada, pancake */

#include <r_core.h>

#define is_in_range(at, from, sz) ((at) >= (from) && (at) < ((from) + (sz)))

#define VA_FALSE    0
#define VA_TRUE     1
#define VA_NOREBASE 2

#define IS_MODE_SET(mode) (mode & R_CORE_BIN_SET)
#define IS_MODE_SIMPLE(mode) (mode & R_CORE_BIN_SIMPLE)
#define IS_MODE_JSON(mode) (mode & R_CORE_BIN_JSON)
#define IS_MODE_RAD(mode) (mode & R_CORE_BIN_RADARE)
#define IS_MODE_NORMAL(mode) (!mode)

// dup from cmd_info
#define PAIR_WIDTH 9
static void pair(const char *a, const char *b, int mode, bool last) {
	if (!b || !(*b)) return;

	if (IS_MODE_JSON (mode)) {
		const char *lst = last ? "" : ",";
		r_cons_printf ("\"%s\":%s%s", a, b, lst);
	} else {
		char ws[16];
		int al = strlen (a);

		al = PAIR_WIDTH - al;
		if (al < 0) al = 0;
		memset (ws, ' ', al);
		ws[al] = 0;
		r_cons_printf ("%s%s%s\n", a, ws, b);
	}
}

static void pair_bool (const char *a, bool t, int mode, bool last) {
	pair (a, r_str_bool (t), mode, last);
}

static void pair_int (const char *a, int n, int mode, bool last) {
	pair (a, sdb_fmt (0, "%d", n), mode, last);
}

static void pair_str (const char *a, const char *b, int mode, int last) {
	if (IS_MODE_JSON (mode)) {
		if (!b) b = "";
		pair (a, sdb_fmt (0, "\"%s\"", b), mode, last);
	} else {
		pair (a, b, mode, last);
	}
}

#define STR(x) (x)?(x):""
// XXX - this may lead to conflicts with set by name
static int r_core_bin_set_cur(RCore *core, RBinFile *binfile);

static ut64 rva(RBin *bin, ut64 paddr, ut64 vaddr, int va) {
	if (va == VA_TRUE) {
		return r_bin_get_vaddr (bin, paddr, vaddr);
	} else if (va == VA_NOREBASE) {
		return vaddr;
	} else {
		return paddr;
	}
}

R_API int r_core_bin_set_by_fd(RCore *core, ut64 bin_fd) {
	if (r_bin_file_set_cur_by_fd (core->bin, bin_fd)) {
		r_core_bin_set_cur (core, r_core_bin_cur(core));
		return true;
	}
	return false;
}

R_API int r_core_bin_set_by_name(RCore *core, const char * name) {
	if (r_bin_file_set_cur_by_name (core->bin, name)) {
		r_core_bin_set_cur (core, r_core_bin_cur (core));
		return true;
	}
	return false;
}

R_API int r_core_bin_set_env(RCore *r, RBinFile *binfile) {
	RBinObject *binobj = binfile ? binfile->o: NULL;
	RBinInfo *info = binobj ? binobj->info: NULL;
	if (info) {
		int va = info->has_va;
		const char * arch = info->arch;
		ut16 bits = info->bits;
		ut64 baseaddr = r_bin_get_baddr (r->bin);
		/* Hack to make baddr work on some corner */
		r_config_set_i (r->config, "io.va",
			(binobj->info)? binobj->info->has_va: 0);
		r_config_set_i (r->config, "bin.baddr", baseaddr);
		r_config_set (r->config, "asm.arch", arch);
		r_config_set_i (r->config, "asm.bits", bits);
		r_config_set (r->config, "anal.arch", arch);
		if (info->cpu && *info->cpu) {
			r_config_set (r->config, "anal.cpu", info->cpu);
		} else {
			r_config_set (r->config, "anal.cpu", arch);
		}
		r_asm_use (r->assembler, arch);

		r_core_bin_info (r, R_CORE_BIN_ACC_ALL, R_CORE_BIN_SET,
			va, NULL, NULL);
		r_core_bin_set_cur (r, binfile);
		return true;
	}
	return false;
}

R_API int r_core_bin_set_cur(RCore *core, RBinFile *binfile) {
	if (!core->bin) return false;
	if (!binfile) {
		// Find first available binfile
		ut32 fd = r_core_file_cur_fd (core);
		binfile = fd != (ut32) -1 ?  r_bin_file_find_by_fd (
			core->bin, fd) : NULL;
		if (!binfile) return false;
	}
	r_bin_file_set_cur_binfile (core->bin, binfile);
	return true;
}

R_API int r_core_bin_refresh_strings(RCore *r) {
	return r_bin_reset_strings (r->bin) ? true: false;
}

R_API RBinFile * r_core_bin_cur(RCore *core) {
	RBinFile *binfile = r_bin_cur (core->bin);
	return binfile;
}

static bool string_filter(RCore *core, const char *str) {
	int i;
	/* pointer/rawdata detection */
	if (core->bin->strpurge) {
		ut8 bo[0x100];
		int up = 0;
		int lo = 0;
		int ot = 0;
		int di = 0;
		int ln = 0;
		int sp = 0;
		int nm = 0;
		for (i = 0; i<0x100; i++) {
			bo[i] = 0;
		}
		for (i = 0; str[i]; i++) {
			if (str[i]>='0' && str[i]<='9')
				nm++;
			else if (str[i]>='a' && str[i]<='z')
				lo++;
			else if (str[i]>='A' && str[i]<='Z')
				up++;
			else ot++;
			if (str[i]=='\\') ot++;
			if (str[i]==' ') sp++;
			bo[(ut8)str[i]] = 1;
			ln++;
		}
		for (i = 0; i<0x100; i++) {
			if (bo[i])
				di++;
		}
		if (ln>2 && str[0] != '_') {
			if (ln<10) return false;
			if (ot >= (nm+up+lo))
				return false;
			if (lo <3)
				return false;
		}
	}

	switch (core->bin->strfilter) {
	case 'a': // only alphanumeric - plain ascii
		for (i = 0; str[i]; i++) {
			char ch = str[i];
			if (ch<0 || !IS_PRINTABLE (ch))
				return false;
		}
		break;
	case 'e': // emails
		if (str && *str) {
			if (!strstr (str+1, "@"))
				return false;
			if (!strstr (str+1, "."))
				return false;
		} else return false;
		break;
	case 'f': // format-string
		if (str && *str) {
			if (!strstr (str+1, "%"))
				return false;
		} else return false;
		break;
	case 'u': // URLs
		if (!strstr (str, "://"))
			return false;
		break;
	case 'p': // path
		if (str[0] != '/')
			return false;
		break;
	case '8': // utf8
		for (i = 0; str[i]; i++) {
			char ch = str[i];
			if (ch<0)
				return true;
		}
		return false;
		break;
	}
	return true;
}

static int bin_strings(RCore *r, int mode, int va) {
	char *q, str[R_FLAG_NAME_SIZE];
	RBinSection *section;
	int hasstr, minstr, maxstr, rawstr;
	RBinString *string;
	RListIter *iter;
	RList *list;
	RBin *bin = r->bin;
	RBinFile * binfile = r_core_bin_cur (r);
	RBinPlugin *plugin = r_bin_file_cur_plugin (binfile);

	if (!binfile) return false;
	minstr = r_config_get_i (r->config, "bin.minstr");
	maxstr = r_config_get_i (r->config, "bin.maxstr");
	rawstr = r_config_get_i (r->config, "bin.rawstr");
	binfile->rawstr = rawstr;

	if (!(hasstr = r_config_get_i (r->config, "bin.strings"))) {
		return 0;
	}

	if (!plugin) return 0;
	if (plugin->info && plugin->name) {
		if (strcmp (plugin->name, "any") == 0 && !rawstr) {
			return false;
		}
	}

	bin->minstrlen = minstr;
	minstr = bin->minstrlen;

	if ((list = r_bin_get_strings (bin)) == NULL) return false;

	if (IS_MODE_JSON (mode)) r_cons_printf ("[");
	if (IS_MODE_RAD (mode)) r_cons_printf ("fs strings");
	if (IS_MODE_SET (mode) && r_config_get_i (r->config, "bin.strings")) {
		r_flag_space_set (r->flags, "strings");
		r_cons_break (NULL, NULL);
	}
	r_list_foreach (list, iter, string) {
		const char *section_name, *type_string;
		ut64 paddr, vaddr, addr;
		if (!string_filter (r, string->string))
			continue;
		paddr = string->paddr;
		vaddr = r_bin_get_vaddr (bin, paddr, string->vaddr);
		addr = va ? vaddr : paddr;

		if (string->length < minstr) continue;
		if (maxstr && string->length > maxstr) continue;

		section = r_bin_get_section_at (r_bin_cur_object (bin), paddr, 0);
		section_name = section ? section->name : "unknown";
		type_string = string->type == 'w' ? "wide" : "ascii";
		if (IS_MODE_SET (mode)) {
			char *f_name;

			if (r_cons_singleton()->breaked) break;
			r_meta_add (r->anal, R_META_TYPE_STRING, addr,
				addr + string->size, string->string);
			f_name = strdup (string->string);
			r_name_filter (f_name, R_FLAG_NAME_SIZE);
			snprintf (str, R_FLAG_NAME_SIZE, "str.%s", f_name);
			r_flag_set (r->flags, str, addr, string->size, 0);
			free (f_name);
		} else if (IS_MODE_SIMPLE (mode)) {
			r_cons_printf ("0x%"PFMT64x" %d %d %s\n", addr,
				string->size, string->length, string->string);
		} else if (IS_MODE_JSON (mode)) {
			q = r_base64_encode_dyn (string->string, -1);
			r_cons_printf ("%s{\"vaddr\":%"PFMT64d
				",\"paddr\":%"PFMT64d",\"ordinal\":%d"
				",\"size\":%d,\"length\":%d,\"section\":\"%s\","
				"\"type\":\"%s\",\"string\":\"%s\"}",
				iter->p ? ",": "",
				vaddr, paddr, string->ordinal, string->size,
				string->length, section_name, type_string, q);
			free (q);
		} else if (IS_MODE_RAD (mode)) {
			char *f_name;

			f_name = strdup (string->string);
			r_name_filter (f_name, R_FLAG_NAME_SIZE);
			snprintf (str, R_FLAG_NAME_SIZE, "str.%s", f_name);
			r_cons_printf ("f str.%s %"PFMT64d" @ 0x%08"PFMT64x"\n"
				"Cs %"PFMT64d" @ 0x%08"PFMT64x"\n",
				f_name, string->size, addr,
				string->size, addr);
			free (f_name);
		} else {
			r_cons_printf ("vaddr=0x%08"PFMT64x" paddr=0x%08"
				PFMT64x" ordinal=%03u sz=%u len=%u "
				"section=%s type=%s string=%s\n",
				vaddr, paddr, string->ordinal, string->size,
				string->length, section_name, type_string,
				string->string);
		}
	}
	if (IS_MODE_JSON (mode)) r_cons_printf ("]");
	if (IS_MODE_SET (mode)) r_cons_break_end ();

	return true;
}

static const char* get_compile_time(Sdb *binFileSdb) {
	Sdb *info_ns = sdb_ns(binFileSdb, "info", false);
	const char *timeDateStamp_string = sdb_const_get (info_ns,
		"image_file_header.TimeDateStamp_string", 0);
	return timeDateStamp_string;
}

static int bin_info(RCore *r, int mode) {
	int i, j, v;
	char str[R_FLAG_NAME_SIZE];
	char size_str[32];
	char baddr_str[32];
	RBinInfo *info = r_bin_get_info (r->bin);
	RBinFile *binfile = r_core_bin_cur (r);
	const char *compiled = NULL;

	if (!binfile || !info) {
		if (mode & R_CORE_BIN_JSON) r_cons_printf ("{}");
		return false;
	}

	compiled = get_compile_time (binfile->sdb);
	snprintf (size_str, sizeof (size_str),
		"%"PFMT64d,  r_bin_get_size (r->bin));
	snprintf (baddr_str, sizeof (baddr_str),
		"%"PFMT64d,  info->baddr);

	if (IS_MODE_SET (mode)) {
		r_config_set (r->config, "file.type", info->rclass);
		r_config_set (r->config, "cfg.bigendian", info->big_endian ? "true" : "false");
		if (info->rclass && !strcmp (info->rclass, "fs")) {
			r_config_set (r->config, "asm.arch", info->arch);
			r_core_cmdf (r, "m /root %s 0", info->arch);
		} else {
			if (info->lang) {
				r_config_set (r->config, "bin.lang", info->lang);
			}
			r_config_set (r->config, "asm.os", info->os);
			r_config_set (r->config, "asm.arch", info->arch);
			r_config_set (r->config, "anal.arch", info->arch);
			snprintf (str, R_FLAG_NAME_SIZE, "%i", info->bits);
			r_config_set (r->config, "asm.bits", str);
			r_config_set (r->config, "asm.dwarf",
				(R_BIN_DBG_STRIPPED &info->dbg_info) ? "false" : "true");
			v = r_anal_archinfo (r->anal, R_ANAL_ARCHINFO_ALIGN);
			if (v != -1) r_config_set_i (r->config, "asm.pcalign", v);
		}
	} else if (IS_MODE_SIMPLE (mode)) {
		r_cons_printf ("arch %s\n", info->arch);
		r_cons_printf ("bits %d\n", info->bits);
		r_cons_printf ("os %s\n", info->os);
		r_cons_printf ("endian %s\n", info->big_endian? "big": "little");
		v = r_anal_archinfo (r->anal, R_ANAL_ARCHINFO_MIN_OP_SIZE);
		if (v != -1) r_cons_printf ("minopsz %d\n", v);
		v = r_anal_archinfo (r->anal, R_ANAL_ARCHINFO_MAX_OP_SIZE);
		if (v != -1) r_cons_printf ("maxopsz %d\n", v);
		v = r_anal_archinfo (r->anal, R_ANAL_ARCHINFO_ALIGN);
		if (v != -1) r_cons_printf ("pcalign %d\n", v);
	} else if (IS_MODE_RAD (mode)) {
		if (info->type && !strcmp (info->type, "fs")) {
			r_cons_printf ("e file.type=fs\n");
			r_cons_printf ("m /root %s 0\n", info->arch);
		} else {
			r_cons_printf ("e cfg.bigendian=%s\n"
				"e asm.bits=%i\n"
				"e asm.dwarf=%s\n",
				r_str_bool (info->big_endian),
				info->bits,
				r_str_bool (R_BIN_DBG_STRIPPED &info->dbg_info));
			if (info->lang && *info->lang) {
				r_cons_printf ("e bin.lang=%s\n", info->lang);
			}
			if (info->rclass && *info->rclass) {
				r_cons_printf ("e file.type=%s\n",
					info->rclass);
			}
			if (info->os) {
				r_cons_printf ("e asm.os=%s\n", info->os);
			}
			if (info->arch) {
				r_cons_printf ("e asm.arch=%s\n", info->arch);
			}
			v = r_anal_archinfo (r->anal, R_ANAL_ARCHINFO_ALIGN);
			if (v != -1) r_cons_printf ("e asm.pcalign=%d\n", v);
		}
	} else {
		// XXX: if type is 'fs' show something different?
		if (IS_MODE_JSON (mode)) r_cons_printf ("{");
		pair_bool ("pic", info->has_pi, mode, false);
		pair_bool ("canary", info->has_canary, mode, false);
		pair_bool ("nx", info->has_nx, mode, false);
		pair_bool ("crypto", info->has_crypto, mode, false);
		pair_bool ("va", info->has_va, mode, false);
		pair_str ("bintype", info->rclass, mode, false);
		pair_str ("class", info->bclass, mode, false);
		pair_str ("lang", info->lang, mode, false);
		pair_str ("arch", info->arch, mode, false);
		pair_int ("bits", info->bits, mode, false);
		pair_str ("machine", info->machine, mode, false);
		pair_str ("os", info->os, mode, false);
		v = r_anal_archinfo (r->anal, R_ANAL_ARCHINFO_MIN_OP_SIZE);
		if (v != -1) pair_int ("minopsz", v, mode, false);
		v = r_anal_archinfo (r->anal, R_ANAL_ARCHINFO_MAX_OP_SIZE);
		if (v != -1) pair_int ("maxopsz", v, mode, false);
		v = r_anal_archinfo (r->anal, R_ANAL_ARCHINFO_ALIGN);
		if (v != -1) pair_int ("pcalign", v, mode, false);
		pair_str ("subsys", info->subsystem, mode, false);
		pair_str ("endian", info->big_endian ? "big" : "little", mode, false);
		pair_bool ("stripped", R_BIN_DBG_STRIPPED & info->dbg_info, mode, false);
		pair_bool ("static", r_bin_is_static (r->bin), mode, false);
		pair_bool ("linenum", R_BIN_DBG_LINENUMS & info->dbg_info, mode, false);
		pair_bool ("lsyms", R_BIN_DBG_SYMS & info->dbg_info, mode, false);
		pair_bool ("relocs", R_BIN_DBG_RELOCS & info->dbg_info, mode, false);
		pair_str ("rpath", info->rpath, mode, false);
		pair_str ("binsz", size_str, mode, false);
		pair_str ("compiled", compiled, mode, false);
		pair_str ("guid", info->guid, mode, false);
		pair_str ("dbg_file", info->debug_file_name, mode, true);

		for (i = 0; info->sum[i].type; i++) {
			int len;

			RBinHash *h = &info->sum[i];
			ut64 hash = r_hash_name_to_bits (h->type);
			RHash *rh = r_hash_new (true, hash);
			len = r_hash_calculate (rh, hash, (const ut8*)
					binfile->buf->buf+h->from, h->to);
			if (len < 1) eprintf ("Invaild wtf\n");
			r_hash_free (rh);

			r_cons_printf ("%s\t%d-%dc\t", h->type, h->from, h->to+h->from);
			for (j = 0; j < h->len; j++) {
				r_cons_printf ("%02x", h->buf[j]);
			}
			r_cons_newline ();
		}
		if (IS_MODE_JSON (mode)) r_cons_printf ("}");
	}
	return true;
}

static int bin_dwarf(RCore *core, int mode) {
	RBinDwarfRow *row;
	RListIter *iter;
	RList *list = NULL;
	RBinFile *binfile = r_core_bin_cur (core);
	RBinPlugin * plugin = r_bin_file_cur_plugin (binfile);
	if (!binfile) return false;

	if (plugin && plugin->lines) {
		list = plugin->lines (binfile);
	} else if (core->bin) {
		// TODO: complete and speed-up support for dwarf
		if (r_config_get_i (core->config, "bin.dwarf")) {
			RBinDwarfDebugAbbrev *da = NULL;
			da = r_bin_dwarf_parse_abbrev (core->bin, mode);
			r_bin_dwarf_parse_info (da, core->bin, mode);
			r_bin_dwarf_parse_aranges (core->bin, mode);
			list = r_bin_dwarf_parse_line (core->bin, mode);
			r_bin_dwarf_free_debug_abbrev (da);
			free (da);
		}
	}
	if (!list) return false;
	r_cons_break (NULL, NULL);
        r_list_foreach (list, iter, row) {
		if (r_cons_singleton()->breaked) break;
		if (mode) {
			// TODO: use 'Cl' instead of CC
			const char *path = row->file;
			char *line = r_file_slurp_line (path, row->line-1, 0);
			if (line) {
				r_str_filter (line, strlen (line));
				line = r_str_replace (line, "\"", "\\\"", 1);
				line = r_str_replace (line, "\\\\", "\\", 1);
			}
			// TODO: implement internal : if ((mode & R_CORE_BIN_SET))
			if ((mode & R_CORE_BIN_SET)) {
				char *cmt = r_str_newf ("%s:%d  %s", row->file, row->line, line?line:"");
				r_meta_set_string (core->anal, R_META_TYPE_COMMENT,
						row->address, cmt);
				free (cmt);
			} else {
				r_cons_printf ("\"CC %s:%d  %s\"@0x%"PFMT64x"\n",
					row->file, row->line, line?line:"", row->address);
			}
			free (line);
		} else {
			r_cons_printf ("0x%08"PFMT64x"\t%s\t%d\n", row->address, row->file, row->line);
		}
        }
	r_cons_break_end ();
	r_list_free (list);
	return true;
}

static int bin_pdb(RCore *core, int mode) {
	R_PDB pdb = {0};
	ut64 baddr = r_bin_get_baddr (core->bin);

	pdb.cb_printf = r_cons_printf;
	if (!init_pdb_parser (&pdb, core->bin->file)) {
		return false;
	}

	if (!pdb.pdb_parse (&pdb)) {
		eprintf ("pdb was not parsed\n");
		pdb.finish_pdb_parse (&pdb);
		return false;
	}

	if (mode == R_CORE_BIN_JSON) r_cons_printf("[");

	switch (mode) {
	case R_CORE_BIN_SET:
		mode = 's';
		r_core_cmd0 (core, ".iP*");
		return true;
	case R_CORE_BIN_JSON:
		mode = 'j';
		break;
	case '*':
	case 1:
		mode = 'r';
		break;
	default:
		mode = 'd'; // default
		break;
	}

	pdb.print_types (&pdb, mode);

	if (mode == 'j') r_cons_printf (",");
	pdb.print_gvars (&pdb, baddr, mode);
	if (mode == 'j') r_cons_printf ("]");
	pdb.finish_pdb_parse (&pdb);

	return true;
}

static int bin_main(RCore *r, int mode, int va) {
	RBinAddr *binmain = r_bin_get_sym (r->bin, R_BIN_SYM_MAIN);
	ut64 addr;
	if (!binmain) return false;

	addr = va ? r_bin_a2b (r->bin, binmain->vaddr) : binmain->paddr;

	if (IS_MODE_SET (mode)) {
		r_flag_space_set (r->flags, "symbols");
		r_flag_set (r->flags, "main", addr, r->blocksize, 0);
	} else if (IS_MODE_SIMPLE (mode)) {
		r_cons_printf ("%"PFMT64d, addr);
	} else if (IS_MODE_RAD (mode)) {
		r_cons_printf ("fs symbols\n");
		r_cons_printf ("f main @ 0x%08"PFMT64x"\n", addr);
	} else if (IS_MODE_JSON (mode)) {
		r_cons_printf ("{\"vaddr\":%" PFMT64d
			",\"paddr\":%" PFMT64d "}", addr, binmain->paddr);
	} else {
		r_cons_printf ("[Main]\n");
		r_cons_printf ("vaddr=0x%08"PFMT64x" paddr=0x%08"PFMT64x"\n",
			addr, binmain->paddr);
	}
	return true;
}

static int bin_entry(RCore *r, int mode, ut64 laddr, int va) {
	char str[R_FLAG_NAME_SIZE];
	RList *entries = r_bin_get_entries (r->bin);
	RListIter *iter;
	RBinAddr *entry = NULL;
	int i = 0;
	ut64 baddr = r_bin_get_baddr (r->bin);

	if (IS_MODE_RAD (mode)) r_cons_printf ("fs symbols\n");
	else if (IS_MODE_JSON (mode)) r_cons_printf ("[");
	else if (IS_MODE_NORMAL (mode)) r_cons_printf ("[Entrypoints]\n");

	r_list_foreach (entries, iter, entry) {
		ut64 paddr = entry->paddr;
		ut64 at = rva (r->bin, paddr, entry->vaddr, va);
		if (IS_MODE_SET (mode)) {
			r_flag_space_set (r->flags, "symbols");
			snprintf (str, R_FLAG_NAME_SIZE, "entry%i", i);
			r_flag_set (r->flags, str, at, 1, 0);
		} else if (IS_MODE_SIMPLE (mode)) {
			r_cons_printf ("0x%08"PFMT64x"\n", at);
		} else if (IS_MODE_JSON (mode)) {
			r_cons_printf ("%s{\"vaddr\":%" PFMT64d ","
				"\"paddr\":%" PFMT64d ","
				"\"baddr\":%" PFMT64d ","
				"\"laddr\":%" PFMT64d "}",
				iter->p ? "," : "", at, paddr, baddr, laddr);
		} else if (IS_MODE_RAD (mode)) {
			r_cons_printf ("f entry%i 1 @ 0x%08"PFMT64x"\n", i, at);
			r_cons_printf ("s entry%i\n", i);
		} else {
			r_cons_printf (
				 "vaddr=0x%08"PFMT64x
				" paddr=0x%08"PFMT64x
				" baddr=0x%08"PFMT64x
				" laddr=0x%08"PFMT64x"\n",
				at, paddr, baddr, laddr);
		}
		i++;
	}
	if (IS_MODE_SET (mode)) {
		if (entry) {
			ut64 at = rva (r->bin, entry->paddr, entry->vaddr, va);
			r_core_seek (r, at, 0);
		}
	} else if (IS_MODE_JSON (mode)) {
		r_cons_printf ("]");
		r_cons_newline ();
	} else if (IS_MODE_NORMAL (mode)) {
		r_cons_printf ("\n%i entrypoints\n", i);
	}
	return true;
}

static const char *bin_reloc_type_name(RBinReloc *reloc) {
#define CASE(T) case R_BIN_RELOC_ ## T: return reloc->additive ? "ADD_" #T : "SET_" #T
	switch (reloc->type) {
		CASE(8);
		CASE(16);
		CASE(32);
		CASE(64);
	}
	return "UNKNOWN";
#undef CASE
}

static ut8 bin_reloc_size(RBinReloc *reloc) {
#define CASE(T) case R_BIN_RELOC_ ## T: return T / 8
	switch (reloc->type) {
		CASE(8);
		CASE(16);
		CASE(32);
		CASE(64);
	}
	return 0;
#undef CASE
}

static char *resolveModuleOrdinal(Sdb *sdb, const char *module, int ordinal) {
	Sdb *db = sdb;
	char *foo = sdb_get (db, sdb_fmt (0, "%d", ordinal), 0);
	return (foo && *foo) ? foo : NULL;
}

static void set_bin_relocs (RCore *r, RBinReloc *reloc, ut64 addr, Sdb **db, char **sdb_module) {
	int bin_demangle = r_config_get_i (r->config, "bin.demangle");
	const char *lang = r_config_get (r->config, "bin.lang");
	char *demname = NULL;
	bool is_pe = true;
	int is_sandbox = r_sandbox_enable (0);

	if (reloc->import && reloc->import->name[0]) {
		char str[R_FLAG_NAME_SIZE];
		RFlagItem *fi;

		if (is_pe && !is_sandbox && strstr (reloc->import->name, "Ordinal")) {
			const char *TOKEN = ".dll_Ordinal_";
			char *module = strdup (reloc->import->name);
			char *import = strstr (module, TOKEN);
			if (import) {
				char *filename;
				int ordinal;
				*import = 0;
				import += strlen (TOKEN);
				ordinal = atoi (import);
				if (!*sdb_module || strcmp (module, *sdb_module)) {
					sdb_free (*db);
					*db = NULL;
					free (*sdb_module);
					*sdb_module = strdup (module);
					filename = sdb_fmt (1, "%s.sdb", module);
					if (r_file_exists (filename)) {
						*db = sdb_new (NULL, filename, 0);
					} else {
#if __WINDOWS__
						filename = sdb_fmt (1, "share/radare2/"R2_VERSION"/format/dll/%s.sdb", module);
#else
						filename = sdb_fmt (1, R2_PREFIX"/share/radare2/" R2_VERSION"/format/dll/%s.sdb", module);
#endif
						if (r_file_exists (filename)) {
							*db = sdb_new (NULL, filename, 0);
						}
					}
				}
				if (*db) {
					// ordinal-1 because we enumerate starting at 0
					char *symname = resolveModuleOrdinal (*db, module, ordinal-1);
					if (symname) {
						snprintf (reloc->import->name,
							sizeof (reloc->import->name),
							"%s.%s", module, symname);
					}
				}
			}
			free (module);
			r_anal_hint_set_size (r->anal, reloc->vaddr, 4);
			r_meta_add (r->anal, R_META_TYPE_DATA, reloc->vaddr, reloc->vaddr+4, NULL);
		}
		snprintf (str, R_FLAG_NAME_SIZE,
			"reloc.%s_%d", reloc->import->name, (int)(addr&0xff));
		if (bin_demangle) {
			demname = r_bin_demangle (r->bin->cur, lang, str);
		}
		r_name_filter (str, 0);
		fi = r_flag_set (r->flags, str, addr, bin_reloc_size (reloc), 0);
		if (demname) {
			r_flag_item_set_name (fi, str,
				sdb_fmt (0, "reloc.%s", demname));
		}
	} else {
		// TODO(eddyb) implement constant relocs.
	}
}

static int bin_relocs(RCore *r, int mode, int va) {
	RList *relocs;
	RListIter *iter;
	RBinReloc *reloc;
	Sdb *db = NULL;
	char *sdb_module = NULL;
	int i = 0;

	va = VA_TRUE; // XXX relocs always vaddr?

	if ((relocs = r_bin_get_relocs (r->bin)) == NULL) return false;

	if (IS_MODE_RAD (mode)) r_cons_printf ("fs relocs\n");
	if (IS_MODE_NORMAL (mode)) r_cons_printf ("[Relocations]\n");
	if (IS_MODE_JSON (mode)) r_cons_printf ("[");
	if (IS_MODE_SET (mode)) r_flag_space_set (r->flags, "relocs");
	r_list_foreach (relocs, iter, reloc) {
		ut64 addr = rva (r->bin, reloc->paddr, reloc->vaddr, va);
		if (IS_MODE_SET (mode)) {
			set_bin_relocs (r, reloc, addr, &db, &sdb_module);
		} else if (IS_MODE_SIMPLE (mode)) {
			r_cons_printf ("0x%08"PFMT64x"  %s\n", addr,
				reloc->import ? reloc->import->name : "");
		} else if (IS_MODE_RAD (mode)) {
			if (reloc->import) {
				char *str = strdup (reloc->import->name);
				r_str_replace_char (str, '$', '_');
				r_cons_printf ("f reloc.%s_%d @ 0x%08"PFMT64x"\n",
					str, (int)(addr & 0xff), addr);
				free (str);
			} else {
				// TODO(eddyb) implement constant relocs.
			}
		} else if (IS_MODE_JSON (mode)) {
			const char *comma = iter->p? ",":"";
			const char *reloc_name = reloc->import ?
				sdb_fmt (0, "\"%s\"", reloc->import->name) :
				"null";
			r_cons_printf ("%s{\"name\":%s,"
				"\"type\":\"%s\","
				"\"vaddr\":%"PFMT64d","
				"\"paddr\":%"PFMT64d"}",
				comma, reloc_name,
				bin_reloc_type_name (reloc),
				reloc->vaddr, reloc->paddr);
		} else if (IS_MODE_NORMAL (mode)) {
			r_cons_printf ("vaddr=0x%08"PFMT64x" paddr=0x%08"PFMT64x" type=%s",
				addr, reloc->paddr, bin_reloc_type_name (reloc));
			if (reloc->import && reloc->import->name[0]) {
				r_cons_printf (" %s", reloc->import->name);
			}
			if (reloc->addend) {
				if (reloc->import && reloc->addend > 0) {
					r_cons_printf (" +");
				}
				if (reloc->addend < 0) {
					r_cons_printf (" - 0x%08"PFMT64x, -reloc->addend);
				} else {
					r_cons_printf (" 0x%08"PFMT64x, reloc->addend);
				}
			}
			r_cons_printf ("\n");
		}
		i++;
	}
	if (IS_MODE_JSON (mode)) r_cons_printf ("]");
	if (IS_MODE_NORMAL (mode)) r_cons_printf ("\n%i relocations\n", i);
	return true;
}

#define MYDB 1
/* this is a hacky workaround that needs proper refactoring in Rbin to use Sdb */
#if MYDB
static Sdb *mydb = NULL;
static RList *osymbols = NULL;
static RBinSymbol *get_symbol(RBin *bin, RList *symbols, const char *name) {
	RBinSymbol *symbol, *res = NULL;
	RListIter *iter;
	if (mydb && symbols != osymbols) {
		sdb_free (mydb);
		mydb = NULL;
		osymbols = symbols;
	}
	if (mydb) {
		res = (RBinSymbol*)(void*)(size_t)
			sdb_num_get (mydb, name, NULL);
	} else {
		mydb = sdb_new0 ();
		r_list_foreach (symbols, iter, symbol) {
			if (!sdb_num_add (mydb, symbol->name, (ut64)(size_t)symbol, 0)) {
			//	eprintf ("DUP (%s)\n", symbol->name);
			}
			if (!res && !strcmp (symbol->name, name)) {
				res = symbol;
			}
		}
	}
	return res;
}
#else
static RBinSymbol *get_symbol(RBin *bin, RList *symbols, const char *name) {
	RBinSymbol *symbol;
	RListIter *iter;
	r_list_foreach (symbols, iter, symbol) {
		if (!strcmp (symbol->name, name))
			return symbol;
	}
	return NULL;
}
#endif

/* XXX: This is a hack to get PLT references in rabin2 -i */
/* imp. is a prefix that can be rewritten by the symbol table */
static ut64 impaddr(RBin *bin, int va, const char *name) {
	char impname[512];
	RList *symbols;
	RBinSymbol *s;
	if (!name || !*name) return false;
	if (!(symbols = r_bin_get_symbols (bin))) {
		return false;
	}
	// TODO: avoid using snprintf here
	snprintf (impname, sizeof (impname), "imp.%s", name);
	s = get_symbol (bin, symbols, impname);
	if (s) {
		if (va) {
			return r_bin_get_vaddr (bin, s->paddr, s->vaddr);
		}
		return s->paddr;
	}
	return 0LL;
}

static int bin_imports(RCore *r, int mode, int va, const char *name) {
	RBinImport *import;
	RListIter *iter;
	RList *imports;
	char *str;
	int i = 0;

	imports = r_bin_get_imports (r->bin);

	if (IS_MODE_JSON (mode)) r_cons_printf ("[");
	else if (IS_MODE_RAD (mode)) r_cons_printf ("fs imports\n");
	else if (IS_MODE_NORMAL (mode)) r_cons_printf ("[Imports]\n");
	r_list_foreach (imports, iter, import) {
		if (name && strcmp (import->name, name)) continue;

		ut64 addr = impaddr (r->bin, va, import->name);
		if (IS_MODE_SET (mode)) {
			// TODO(eddyb) symbols that are imports.
		} else if (IS_MODE_SIMPLE (mode)) {
			r_cons_printf ("%s\n", import->name);
		} else if (IS_MODE_JSON (mode)) {
			str = r_str_utf16_encode (import->name, -1);
			str = r_str_replace (str, "\"", "\\\"", 1);
			addr = impaddr (r->bin, va, import->name);
			r_cons_printf ("%s{\"ordinal\":%d,"
				"\"bind\":\"%s\","
				"\"type\":\"%s\",",
				iter->p ? "," : "",
				import->ordinal,
				import->bind,
				import->type);
			if (import->classname[0]) {
				r_cons_printf ("\"classname\":\"%s\","
					"\"descriptor\":\"%s\",",
					import->classname,
					import->descriptor);
			}
			r_cons_printf("\"name\":\"%s\","
				"\"plt\":%"PFMT64d"}",
				str, addr);
			free (str);
		} else if (IS_MODE_RAD (mode)) {
			// TODO(eddyb) symbols that are imports.
		} else {
			r_cons_printf ("ordinal=%03d plt=0x%08"PFMT64x" bind=%s type=%s",
				import->ordinal, addr, import->bind, import->type);
			if (import->classname[0]) {
				r_cons_printf (" classname=%s", import->classname);
			}
			r_cons_printf (" name=%s", import->name);
			if (import->descriptor[0]) {
				r_cons_printf (" descriptor=%s", import->descriptor);
			}
			r_cons_printf ("\n");
		}
		i++;
	}
	if (IS_MODE_JSON (mode)) r_cons_printf ("]");
	else if (IS_MODE_NORMAL (mode)) r_cons_printf ("\n%i imports\n", i);
#if MYDB
	osymbols = NULL;
	sdb_free (mydb);
	mydb = NULL;
#endif
	return true;
}

static const char *getPrefixFor(const char *s) {
	if (s) {
		if (!strcmp (s, "NOTYPE")) {
			return "loc";
		} else if (!strcmp (s, "OBJECT")) {
			return "obj";
		}
	}
	return "sym";
}

typedef struct {
	ut64 addr;
	const char *pfx; // prefix for flags
	char *name;      // raw symbol name
	char *nameflag;  // flag name for symbol
	char *demname;   // demangled raw symbol name
	char *demflag;   // flag name for demangled symbol
	char *classname; // classname
	char *classflag; // flag for classname
	char *methname;  // methods [class]::[method]
	char *methflag;  // methods flag sym.[class].[method]
} SymName;

static void snInit(RCore *r, SymName *sn, RBinSymbol *sym, const char *lang) {
#define MAXFLAG_LEN 128
	int bin_demangle = lang != NULL;
	const char *pfx = getPrefixFor (sym->type);
	sn->name = strdup (sym->name);
	sn->nameflag = r_str_newf ("%s.%s", pfx, sym->name);
	r_name_filter (sn->nameflag, MAXFLAG_LEN);
	if (sym->classname[0]) {
		sn->classname = strdup (sym->classname);
		sn->classflag = r_str_newf ("sym.%s.%s", sn->classname, sn->name);
		r_name_filter (sn->classflag, MAXFLAG_LEN);

		sn->methname = r_str_newf ("%s::%s", sn->classname, sym->name);
		sn->methflag = r_str_newf ("sym.%s.%s", sn->classname, sn->name);
		r_name_filter (sn->methflag, MAXFLAG_LEN);
	} else {
		sn->classname = NULL;
		sn->classflag = NULL;
		sn->methname = NULL;
		sn->methflag = NULL;
	}
	sn->demname = NULL;
	sn->demflag = NULL;
	if (bin_demangle) {
		sn->demname = r_bin_demangle (r->bin->cur, lang, sn->name);
		if (sn->demname) {
			sn->demflag = r_str_newf ("%s.%s", pfx, sn->demname);
			r_name_filter (sn->demflag, MAXFLAG_LEN);
		}
	}
}

static void snFini(SymName *sn) {
	R_FREE (sn->name);
	R_FREE (sn->nameflag);
	R_FREE (sn->demname);
	R_FREE (sn->demflag);
	R_FREE (sn->classname);
	R_FREE (sn->classflag);
	R_FREE (sn->methname);
	R_FREE (sn->methflag);
}

static bool isAnExport(RBinSymbol *s) {
	/* workaround for some bin plugs */
	if (strncmp (s->name, "imp.", 4) == 0) return false;
	return (strcmp (s->bind, "GLOBAL") == 0);
}

static int bin_symbols_internal(RCore *r, int mode, ut64 laddr, int va, ut64 at, const char *name, bool exponly) {
	RBinInfo *info = r_bin_get_info (r->bin);
	int is_arm = info && info->arch && !strcmp (info->arch, "arm");
	int bin_demangle = r_config_get_i (r->config, "bin.demangle");
	RBinSymbol *symbol;
	const char *lang;
	RListIter *iter;
	RList *symbols;
	int lastfs = 's';
	int i = 0;

	lang = bin_demangle ? r_config_get (r->config, "bin.lang") : NULL;

	symbols = r_bin_get_symbols (r->bin);
	r_space_set (&r->anal->meta_spaces, "bin");

	if (IS_MODE_JSON (mode)) r_cons_printf ("[");
	else if (IS_MODE_SET (mode)) r_flag_space_set (r->flags, "symbols");
	else if (!at && exponly) {
		if (IS_MODE_RAD (mode)) r_cons_printf ("fs exports\n");
		if (IS_MODE_NORMAL (mode)) r_cons_printf ("[Exports]\n");
	} else if (!at && !exponly) {
		if (IS_MODE_RAD (mode)) r_cons_printf ("fs symbols\n");
		if (IS_MODE_NORMAL (mode)) r_cons_printf ("[Symbols]\n");
	}
	r_list_foreach (symbols, iter, symbol) {
		ut64 addr = rva (r->bin, symbol->paddr, symbol->vaddr, va);
		SymName sn;

		if (exponly && !isAnExport (symbol)) continue;
		if (name && strcmp (symbol->name, name)) continue;
		if (at && (symbol->size == 0 || !is_in_range (at, addr, symbol->size))) {
			continue;
		}

		snInit (r, &sn, symbol, lang);

		if (IS_MODE_SET (mode)) {
			if (is_arm) {
				int force_bits = 0;
				if (va && symbol->bits == 16)
					force_bits = 16;
				if (info->bits == 16 && symbol->bits == 32)
					force_bits = 32;
				r_anal_hint_set_bits (r->anal, addr, force_bits);
			}

			if (!strncmp (symbol->name, "imp.", 4)) {
				if (lastfs != 'i')
					r_flag_space_set (r->flags, "imports");
				lastfs = 'i';
			} else {
				if (lastfs != 's')
					r_flag_space_set (r->flags, "symbols");
				lastfs = 's';
			}
			/* If that's a Classed symbol (method or so) */
			if (sn.classname) {
				RFlagItem *fi = NULL;
				char *comment = NULL;
				fi = r_flag_get (r->flags, sn.methflag);
				if (fi) {
					r_flag_item_set_name (fi, sn.methflag, sn.methname);
					if ((fi->offset - r->flags->base) == addr) {
						comment = fi->comment ? strdup (fi->comment) : NULL;
						r_flag_unset (r->flags, sn.methflag, fi);
						fi = NULL;
					}
				} else {
					fi = r_flag_set (r->flags, sn.methflag, addr, symbol->size, 0);
					comment = fi->comment ? strdup (fi->comment) : NULL;
					if (comment) {
						r_flag_item_set_comment (fi, comment);
						R_FREE (comment);
					}
				}
			} else {
				const char *fn, *n;
				RFlagItem *fi;
				n = sn.demname? sn.demname: sn.name;
				fn = sn.demflag? sn.demflag: sn.nameflag;
				fi = r_flag_set (r->flags, fn, addr, symbol->size, 0);
				if (fi) {
					r_flag_item_set_name (fi, fn, n);
				} else {
					eprintf ("== Cant find flag (%s)\n", fn);
				}
			}
			if (sn.demname) {
				r_meta_add (r->anal, R_META_TYPE_COMMENT,
					addr, symbol->size, sn.demname);
			}
		} else if (IS_MODE_JSON (mode)) {
			char *str = r_str_utf16_encode (symbol->name, -1);
			str = r_str_replace (str, "\"", "\\\"", 1);
			r_cons_printf ("%s{\"name\":\"%s\","
				"\"demname\":\"%s\","
				"\"flagname\":\"%s\","
				"\"size\":%d,"
				"\"vaddr\":%"PFMT64d","
				"\"paddr\":%"PFMT64d"}",
				iter->p?",":"", str,
				sn.demname? sn.demname: "",
				sn.nameflag,
				(int)symbol->size,
				(ut64)addr, (ut64)symbol->paddr);
			free (str);
		} else if (IS_MODE_SIMPLE (mode)) {
			char *name = strdup (symbol->name);
			if (bin_demangle) {
				const char *symname = name;
				char *dname = r_bin_demangle (r->bin->cur, lang, symname);
				if (dname) {
					free (name);
					name = dname;
				}
			}
			//r_name_filter (name, 80);
			r_cons_printf ("0x%08"PFMT64x" %d %s\n",
				addr, (int)symbol->size, name);
			free (name);
		} else if (IS_MODE_RAD (mode)) {
			RBinFile *binfile;
			RBinPlugin *plugin;
			char *name;

			if (!strcmp (symbol->type, "NOTYPE")) {
				continue;
			}
			if (bin_demangle) {
				char *mn = r_bin_demangle (r->bin->cur, lang, symbol->name);
				if (mn) {
					r_cons_printf ("s 0x%08"PFMT64x"\n\"CC %s\"\n",
							symbol->paddr, mn);
					name = mn;
				} else {
					name = strdup (symbol->name);
				}
			} else {
				name = strdup (symbol->name);
			}
			r_name_filter (name, -1);
			if (!strncmp (name, "imp.", 4)) {
				if (lastfs != 'i')
					r_cons_printf ("fs imports\n");
				lastfs = 'i';
			} else {
				if (lastfs != 's') {
					if (exponly) {
						r_cons_printf ("fs exports\n");
					} else {
						r_cons_printf ("fs symbols\n");
					}
				}
				lastfs = 's';
			}
			r_cons_printf ("f sym.%s %u 0x%08"PFMT64x"\n",
					name, symbol->size, addr);
			binfile = r_core_bin_cur (r);
			plugin = r_bin_file_cur_plugin (binfile);
			if (plugin && plugin->name) {
				if (!strncmp (plugin->name, "pe", 2)) {
					char *p, *module = strdup (symbol->name);
					p = strstr (module, ".dll_");
					if (p) {
						const char *symname = p+5;
						*p = 0;
						r_cons_printf ("k bin/pe/%s/%d=%s\n",
							module, symbol->ordinal, symname);
					}
					free (module);
				}
			}
		} else {
			const char *name = symbol->name;
			char *mn = NULL;
			if (bin_demangle) {
				mn = r_bin_demangle (r->bin->cur, lang, symbol->name);
				if (mn) name = mn;
			}
			r_cons_printf ("vaddr=0x%08"PFMT64x" paddr=0x%08"PFMT64x" ord=%03u "
				"fwd=%s sz=%u bind=%s type=%s name=%s\n",
				addr, symbol->paddr,
				symbol->ordinal, symbol->forwarder,
				symbol->size, symbol->bind, symbol->type,
				name);
			free (mn);
		}
		snFini (&sn);
		i++;
	}
	if (IS_MODE_JSON (mode)) r_cons_printf ("]");
	if (IS_MODE_NORMAL (mode) && !at) {
		r_cons_printf ("\n%i %s\n", i, exponly ? "exports" : "symbols");
	}

	r_space_set (&r->anal->meta_spaces, NULL);
	return true;
}

static int bin_exports(RCore *r, int mode, ut64 laddr, int va, ut64 at, const char *name) {
	return bin_symbols_internal (r, mode, laddr, va, at, name, true);
}

static int bin_symbols(RCore *r, int mode, ut64 laddr, int va, ut64 at, const char *name) {
	return bin_symbols_internal (r, mode, laddr, va, at, name, false);
}


static char *build_hash_string(int mode, const char *chksum, ut8 *data, ut32 datalen) {

	char *chkstr = NULL, *aux, *ret = NULL;
	char tmp[128];
	const char *ptr = chksum;
	int i;
	do {
		for (i = 0; *ptr && *ptr != ',' && i < sizeof(tmp) -1; i++)
			tmp[i] = *ptr++;
		tmp[i] = '\0';
		chkstr = r_hash_to_string (NULL, tmp, data, datalen);
		if (!chkstr) {
			if (*ptr && *ptr == ',') ptr++;
			continue;
		}
		if (IS_MODE_SIMPLE (mode)) {
			aux = r_str_newf ("%s ", chkstr);
		} else if (IS_MODE_JSON (mode)) {
			aux = r_str_newf ("\"%s\":\"%s\",", tmp, chkstr);
		} else {
			aux = r_str_newf ("%s=%s ", tmp, chkstr);
		}
		ret = r_str_concat (ret, aux);
		free (chkstr);
		free (aux);
		if (*ptr && *ptr == ',') ptr++;
	} while (*ptr);

	return ret;
}

static int bin_sections(RCore *r, int mode, ut64 laddr, int va, ut64 at, const char *name, const char *chksum) {
	char str[R_FLAG_NAME_SIZE];
	RBinSection *section;
	RBinInfo *info = NULL;
	RList *sections;
	RListIter *iter;
	int i = 0;
	int fd = -1;

	sections = r_bin_get_sections (r->bin);

	if (IS_MODE_JSON (mode)) r_cons_printf ("[");
	else if (IS_MODE_RAD (mode) && !at) r_cons_printf ("fs sections\n");
	else if (IS_MODE_NORMAL (mode) && !at) r_cons_printf ("[Sections]\n");
	else if (IS_MODE_SET (mode)) {
		fd = r_core_file_cur_fd (r);
		r_flag_space_set (r->flags, "sections");
	}
	r_list_foreach (sections, iter, section) {
		char perms[] = "-----";
		int va_sect = va;
		ut64 addr;

		if (va && !(section->srwx & R_BIN_SCN_READABLE)) {
			va_sect = VA_NOREBASE;
		}
		addr = rva (r->bin, section->paddr, section->vaddr, va_sect);

		if (name && strcmp (section->name, name)) continue;
		r_name_filter (section->name, sizeof (section->name));
		if (at && (section->size == 0 || !is_in_range (at, addr, section->size))) {
			continue;
		}

		if (section->srwx & R_BIN_SCN_MAP) perms[0] = 'm';
		if (section->srwx & R_BIN_SCN_SHAREABLE) perms[1] = 's';
		if (section->srwx & R_BIN_SCN_READABLE) perms[2] = 'r';
		if (section->srwx & R_BIN_SCN_WRITABLE) perms[3] = 'w';
		if (section->srwx & R_BIN_SCN_EXECUTABLE) perms[4] = 'x';

		if (IS_MODE_SET (mode)) {
#if LOAD_BSS_MALLOC
			if (!strcmp (section->name, ".bss")) {
				// check if there's already a file opened there
				int loaded = 0;
				RListIter *iter;
				RIOMap *m;
				r_list_foreach (r->io->maps, iter, m) {
					if (m->from == addr) {
						loaded = 1;
					}
				}
				if (!loaded) {
					r_core_cmdf (r, "on malloc://%d 0x%"PFMT64x" # bss\n",
						section->vsize, addr);
				}
			}
#endif
			r_name_filter (section->name, 128);
			snprintf (str, sizeof(str)-1, "section.%s", section->name);
			r_flag_set (r->flags, str, addr, section->size, 0);
			snprintf (str, sizeof(str)-1, "section_end.%s", section->name);
			r_flag_set (r->flags, str, addr + section->size, 0, 0);
			if (section->arch || section->bits) {
				const char *arch = section->arch;
				int bits = section->bits;
				if (!arch) arch = info->arch;
				if (!bits) bits = info->bits;
				//r_io_section_set_archbits (r->io, addr, arch, bits);
			}
			snprintf (str, sizeof (str)-1, "[%i] va=0x%08"PFMT64x" pa=0x%08"PFMT64x" sz=%"
				PFMT64d" vsz=%"PFMT64d" rwx=%s %s",
				i, addr, section->paddr, section->size, section->vsize,
				perms, section->name);
			r_meta_add (r->anal, R_META_TYPE_COMMENT, addr, addr, str);
			r_io_section_add (r->io, section->paddr, addr, section->size,
				section->vsize, section->srwx, section->name, 0, fd);
		} else if (IS_MODE_SIMPLE (mode)) {
			char *hashstr = NULL;
			if (chksum) {
				ut8 *data = malloc (section->size);
				if (!data) return false;
				ut32 datalen = section->size;
				r_io_pread (r->io, section->paddr, data, datalen);
				hashstr = build_hash_string (mode, chksum,
							data, datalen);
				free (data);
			}
			r_cons_printf ("0x%"PFMT64x" 0x%"PFMT64x" %s %s%s%s\n",
				addr, addr + section->size,
				perms,
				hashstr ? hashstr : "", hashstr ? " " : "",
				section->name
			);
			free (hashstr);
		} else if (IS_MODE_JSON (mode)) {
			char *hashstr = NULL;
			if (chksum) {
				ut8 *data = malloc (section->size);
				if (!data) return false;
				ut32 datalen = section->size;
				r_io_pread (r->io, section->paddr, data, datalen);
				hashstr = build_hash_string (mode, chksum,
							data, datalen);
				free (data);

			}
			r_cons_printf ("%s{\"name\":\"%s\","
				"\"size\":%"PFMT64d","
				"\"vsize\":%"PFMT64d","
				"\"flags\":\"%s\","
				"%s"
				"\"paddr\":%"PFMT64d","
				"\"vaddr\":%"PFMT64d"}",
				iter->p?",":"",
				section->name,
				section->size,
				section->vsize,
				perms,
				hashstr ? hashstr : "",
				section->paddr,
				addr);
			free (hashstr);
		} else if (IS_MODE_RAD (mode)) {
			if (!strcmp (section->name, ".bss")) {
				r_cons_printf ("on malloc://%d 0x%"PFMT64x" # bss\n",
						section->vsize, addr);
			}
			r_cons_printf ("S 0x%08"PFMT64x" 0x%08"PFMT64x" 0x%08"PFMT64x" 0x%08"PFMT64x" %s %d\n",
				section->paddr, addr, section->size, section->vsize,
				section->name, (int)section->srwx);
			if (section->arch || section->bits) {
				const char *arch = section->arch;
				int bits = section->bits;
				if (!arch) arch = info->arch;
				if (!bits) bits = info->bits;
				r_cons_printf ("Sa %s %d @ 0x%08"
					PFMT64x"\n", arch, bits, addr);
			}
			r_cons_printf ("f section.%s %"PFMT64d" 0x%08"PFMT64x"\n",
					section->name, section->size, addr);
			r_cons_printf ("f section_end.%s 1 0x%08"PFMT64x"\n",
					section->name, addr + section->size);
			r_cons_printf ("CC [%02i] va=0x%08"PFMT64x" pa=0x%08"PFMT64x" sz=%"PFMT64d" vsz=%"PFMT64d" "
					"rwx=%s %s @ 0x%08"PFMT64x"\n",
					i, addr, section->paddr, section->size, section->vsize,
					perms, section->name, addr);
		} else {
			char *hashstr = NULL, str[128];
			if (chksum) {
				ut8 *data = malloc (section->size);
				if (!data) return false;
				ut32 datalen = section->size;
				// VA READ IS BROKEN?
				r_io_pread (r->io, section->paddr, data, datalen);
				hashstr = build_hash_string (mode, chksum,
							data, datalen);
				free (data);
			}
			if (section->arch || section->bits) {
				const char *arch = section->arch;
				int bits = section->bits;
				if (!arch) arch = info->arch;
				if (!bits) bits = info->bits;
				snprintf (str, sizeof (str), "arch=%s bits=%d ", arch, bits);
			} else str[0] = 0;
			r_cons_printf ("idx=%02i vaddr=0x%08"PFMT64x" paddr=0x%08"PFMT64x" sz=%"PFMT64d" vsz=%"PFMT64d" "
				"perm=%s %s%sname=%s\n",
				i, addr, section->paddr, section->size, section->vsize,
				perms, str, hashstr ?hashstr : "", section->name);
			free (hashstr);
		}
		i++;
	}
	if (IS_MODE_JSON (mode)) r_cons_printf ("]\n");
	else if (IS_MODE_NORMAL (mode) && !at) r_cons_printf ("\n%i sections\n", i);

	return true;
}

static int bin_fields(RCore *r, int mode, int va) {
	RList *fields;
	RListIter *iter;
	RBinField *field;
	int i = 0;
	RBin *bin = r->bin;
	RBinFile *binfile = r_core_bin_cur (r);
	ut64 size = binfile ? binfile->size : UT64_MAX;
	ut64 baddr = r_bin_get_baddr (r->bin);

	if ((fields = r_bin_get_fields (bin)) == NULL)
		return false;

	if (IS_MODE_JSON (mode)) r_cons_printf ("[");
	else if (IS_MODE_RAD (mode)) r_cons_printf ("fs header\n");
	else if (IS_MODE_NORMAL (mode)) r_cons_printf ("[Header fields]\n");
	else if (IS_MODE_SET (mode)) {
		// XXX: Need more flags??
		// this will be set even if the binary does not have an ehdr
		int fd = r_core_file_cur_fd(r);
		r_io_section_add (r->io, 0, baddr, size, size, 7, "ehdr", 0, fd);
	}
	r_list_foreach (fields, iter, field) {
		ut64 addr = rva (bin, field->paddr, field->vaddr, va);

		if (IS_MODE_RAD (mode)) {
			r_name_filter (field->name, sizeof (field->name));
			r_cons_printf ("f header.%s @ 0x%08"PFMT64x"\n", field->name, addr);
			r_cons_printf ("[%02i] vaddr=0x%08"PFMT64x" paddr=0x%08"PFMT64x" name=%s\n",
				i, addr, field->paddr, field->name);
		} else if (IS_MODE_JSON (mode)) {
			r_cons_printf ("%s{\"name\":\"%s\","
				"\"paddr\":%"PFMT64d"}",
				iter->p?",":"",
				field->name, addr);
		} else if (IS_MODE_NORMAL (mode)) {
			r_cons_printf ("idx=%02i vaddr=0x%08"PFMT64x" paddr=0x%08"PFMT64x" name=%s\n",
				i, addr, field->paddr, field->name);
		}
		i++;
	}
	if (IS_MODE_JSON (mode)) r_cons_printf ("]");
	else if (IS_MODE_RAD (mode)) {
		/* add program header section */
		r_cons_printf ("S 0 0x%"PFMT64x" 0x%"PFMT64x" 0x%"PFMT64x" ehdr rwx\n",
			baddr, size, size);
	} else if (IS_MODE_NORMAL (mode)) {
		r_cons_printf ("\n%i fields\n", i);
	}

	return true;
}

static int bin_classes(RCore *r, int mode) {
	RListIter *iter, *iter2;
	RBinSymbol *sym;
	RBinClass *c;
	RList *cs = r_bin_get_classes (r->bin);
	if (!cs) return false;

	// XXX: support for classes is broken and needs more love
	if (IS_MODE_JSON (mode)) r_cons_printf ("[");
	else if (IS_MODE_SET (mode)) {
		if (!r_config_get_i (r->config, "bin.classes")) return false;
		r_flag_space_set (r->flags, "classes");
	} else if (IS_MODE_RAD (mode)) r_cons_printf ("fs classes\n");
	r_list_foreach (cs, iter, c) {
		char *name;

		if (!c->name || !c->name[0]) continue;
		name = strdup (c->name);
		r_name_filter (name, 0);

		if (IS_MODE_SET (mode)) {
			char str[R_FLAG_NAME_SIZE + 1];

			snprintf (str, R_FLAG_NAME_SIZE, "class.%s", name);
			r_flag_set (r->flags, str, c->addr, 1, 0);
			r_list_foreach (c->methods, iter2, sym) {
				snprintf (str, sizeof (str),
					"method.%s.%s", c->name, sym->name);
				r_name_filter (str, 0);
				r_flag_set (r->flags, str, sym->vaddr, 1, 0);
			}
		} else if (IS_MODE_SIMPLE (mode)) {
			r_cons_printf ("0x%08"PFMT64x" %s%s%s\n",
				c->addr, c->name, c->super ? " " : "",
				c->super ? c->super : "");
		} else if (IS_MODE_RAD (mode)) {
			r_cons_printf ("f class.%s = 0x%"PFMT64x"\n",
				name, c->addr);
			if (c->super) {
				r_cons_printf ("f super.%s.%s = %d\n",
					c->name, c->super, c->index);
			}
			r_list_foreach (c->methods, iter2, sym) {
				r_cons_printf ("f method.%s.%s = 0x%"PFMT64x"\n",
					c->name, sym->name, sym->vaddr);
			}
		} else if (IS_MODE_JSON (mode)) {
			if (c->super) {
				r_cons_printf ("%s{\"name\":\"%s\",\"addr\":%"PFMT64d",\"index\":%"PFMT64d",\"super\":\"%s\"}",
					iter->p ? "," : "", c->name, c->addr,
					c->index, c->super);
			} else {
				r_cons_printf ("%s{\"name\":\"%s\",\"addr\":%"PFMT64d",\"index\":%"PFMT64d"}",
					iter->p ? "," : "", c->name, c->addr,
					c->index);
			}
		} else {
			r_cons_printf ("0x%08"PFMT64x" class %d %s",
				c->addr, c->index, c->name);
			if (c->super) {
				r_cons_printf (" super: %s\n", c->super);
			}
			r_cons_newline();
			int m = 0;
			r_list_foreach (c->methods, iter2, sym) {
				r_cons_printf ("0x%08"PFMT64x" method %d %s\n",
					sym->vaddr, m, sym->name);
				m++;
			}
			r_cons_newline ();
		}

		free (name);
	}
	if (IS_MODE_JSON (mode)) r_cons_printf ("]");

	return true;
}

static int bin_size(RCore *r, int mode) {
	int size = r_bin_get_size (r->bin);
	if (IS_MODE_SIMPLE (mode) || IS_MODE_JSON (mode)) {
		r_cons_printf ("%d\n", size);
	} else if (IS_MODE_RAD (mode)) {
		r_cons_printf ("f bin_size @ %d\n", size);
	} else if (IS_MODE_SET (mode)) {
		r_core_cmdf (r, "f bin_size @ %d\n", size);
	} else {
		r_cons_printf ("%d\n", size);
	}
	return true;
}

static int bin_libs(RCore *r, int mode) {
	RList *libs;
	RListIter *iter;
	char* lib;
	int i = 0;

	if ((libs = r_bin_get_libs (r->bin)) == NULL) return false;

	if (IS_MODE_JSON (mode)) r_cons_printf ("[");
	else if (IS_MODE_NORMAL (mode)) r_cons_printf ("[Linked libraries]\n");
	r_list_foreach (libs, iter, lib) {
		if (IS_MODE_SET (mode)) {
			// Nothing to set.
			// TODO: load libraries with iomaps?
		} else if (IS_MODE_RAD (mode)) {
			r_cons_printf ("CCa entry0 %s\n", lib);
		} else if (IS_MODE_JSON (mode)) {
			r_cons_printf ("%s\"%s\"", iter->p ? "," : "", lib);
		} else {
			// simple and normal print mode
			r_cons_printf ("%s\n", lib);
		}
		i++;
	}
	if (IS_MODE_JSON (mode)) r_cons_printf ("]");
	else if (IS_MODE_NORMAL (mode)) {
		if (i == 1) r_cons_printf ("\n%i library\n", i);
		else r_cons_printf ("\n%i libraries\n", i);
	}

	return true;
}

static void bin_mem_print(RList *mems, int perms, int depth) {
	RBinMem *mem;
	RListIter *iter;
	int i;

	if (!mems) return;

	r_list_foreach (mems, iter, mem) {
		if (mem) {
			for (i = 0; i < depth; i++) {
				r_cons_printf (" ");
			}
			r_cons_printf ("%8s addr=0x%016"PFMT64x" size=%6d perms=[%s]\n",
				mem->name, mem->addr, mem->size, r_str_rwx_i (mem->perms & perms));
			if (mem->mirrors) {
				bin_mem_print (mem->mirrors, (mem->perms & perms), (depth + 1));	//sorry, but anything else would be inefficient
			}
		}
	}
}

static int bin_mem(RCore *r, int mode) {
	RList *mem = NULL;
	if (!r)	return false;
	if (!(IS_MODE_RAD (mode) || IS_MODE_SET (mode))) {
		r_cons_printf ("[Memory]\n\n");
	}
	if (!(mem = r_bin_get_mem (r->bin))) {
		return false;
	}
	if (IS_MODE_JSON (mode)) {
		r_cons_printf ("TODO\n");
		return false;
	}
	if (!(IS_MODE_RAD (mode) || IS_MODE_SET (mode))) {
		bin_mem_print (mem, 7, 0);
	}
	return true;
}

R_API int r_core_bin_info(RCore *core, int action, int mode, int va, RCoreBinFilter *filter, const char *chksum) {
	int ret = true;
	const char *name = NULL;
	ut64 at = 0, loadaddr = r_bin_get_laddr (core->bin);

	if (filter && filter->offset) at = filter->offset;
	if (filter && filter->name) name = filter->name;

	// use our internal values for va
	va = va ? VA_TRUE : VA_FALSE;

	if ((action & R_CORE_BIN_ACC_STRINGS))
		ret &= bin_strings (core, mode, va);
	if ((action & R_CORE_BIN_ACC_INFO))
		ret &= bin_info (core, mode);
	if ((action & R_CORE_BIN_ACC_MAIN))
		ret &= bin_main (core, mode, va);
	if ((action & R_CORE_BIN_ACC_DWARF))
		ret &= bin_dwarf (core, mode);
	if ((action & R_CORE_BIN_ACC_PDB))
		ret &= bin_pdb (core, mode);
	if ((action & R_CORE_BIN_ACC_ENTRIES))
		ret &= bin_entry (core, mode, loadaddr, va);
	if ((action & R_CORE_BIN_ACC_RELOCS))
		ret &= bin_relocs (core, mode, va);
	if ((action & R_CORE_BIN_ACC_IMPORTS))
		ret &= bin_imports (core, mode, va, name);
	if ((action & R_CORE_BIN_ACC_EXPORTS))
		ret &= bin_exports (core, mode, loadaddr, va, at, name);
	if ((action & R_CORE_BIN_ACC_SYMBOLS))
		ret &= bin_symbols (core, mode, loadaddr, va, at, name);
	if ((action & R_CORE_BIN_ACC_SECTIONS))
		ret &= bin_sections (core, mode, loadaddr, va, at, name, chksum);
	if ((action & R_CORE_BIN_ACC_FIELDS))
		ret &= bin_fields (core, mode, va);
	if ((action & R_CORE_BIN_ACC_LIBS))
		ret &= bin_libs (core, mode);
	if ((action & R_CORE_BIN_ACC_CLASSES))
		ret &= bin_classes (core, mode);
	if ((action & R_CORE_BIN_ACC_SIZE))
		ret &= bin_size (core, mode);
	if ((action & R_CORE_BIN_ACC_MEM))
		ret &= bin_mem (core, mode);
	return ret;
}

R_API int r_core_bin_set_arch_bits(RCore *r, const char *name, const char * arch, ut16 bits) {
	RCoreFile *cf = r_core_file_cur (r);
	RBinFile *binfile;

	if (!name) {
		name = (cf && cf->desc) ? cf->desc->name : NULL;
	}
	if (!name) return false;

	/* Check if the arch name is a valid name */
	if (!r_asm_is_valid (r->assembler, arch)) return false;

	/* Find a file with the requested name/arch/bits */
	binfile = r_bin_file_find_by_arch_bits (r->bin, arch, bits, name);
	if (!binfile) return false;

	if (!r_bin_use_arch (r->bin, arch, bits, name)) {
		return false;
	}

	r_core_bin_set_cur (r, binfile);
	return r_core_bin_set_env (r, binfile);
}

R_API int r_core_bin_update_arch_bits(RCore *r) {
	RBinFile *binfile = r_core_bin_cur (r);
	const char * arch = r->assembler->cur->arch;
	ut16 bits = r->assembler->bits;
	const char *name = binfile ? binfile->file : NULL;
	return r_core_bin_set_arch_bits (r, name, arch, bits);
}

R_API int r_core_bin_raise(RCore *core, ut32 binfile_idx, ut32 binobj_idx) {
	RBin *bin = core->bin;
	RBinFile *binfile = NULL;

	if (binfile_idx == UT32_MAX && binobj_idx == UT32_MAX) {
		return false;
	}

	if (!r_bin_select_by_ids (bin, binfile_idx, binobj_idx)) return false;
	binfile = r_core_bin_cur (core);
	if (binfile) {
		r_io_raise (core->io, binfile->fd);
	}
	core->switch_file_view = 1;
	return binfile && r_core_bin_set_env (core, binfile) && r_core_block_read (core, 0);
}

R_API int r_core_bin_delete(RCore *core, ut32 binfile_idx, ut32 binobj_idx) {
	RBin *bin = core->bin;
	RBinFile *binfile = NULL;

	if (binfile_idx == UT32_MAX && binobj_idx == UT32_MAX) return false;
	if (!r_bin_object_delete (bin, binfile_idx, binobj_idx)) return false;

	binfile = r_core_bin_cur (core);
	if (binfile) {
		r_io_raise (core->io, binfile->fd);
	}
	core->switch_file_view = 1;
	return binfile && r_core_bin_set_env (core, binfile) && r_core_block_read (core, 0);
}

static int r_core_bin_file_print(RCore *core, RBinFile *binfile, int mode) {
	RListIter *iter;
	RBinObject *obj;
	const char *name = binfile ? binfile->file : NULL;
	ut32 id = binfile ? binfile->id : 0;
	ut32 fd = binfile ? binfile->fd : 0;
	ut32 obj_cnt = binfile ? r_list_length (binfile->objs) : 0;
	ut32 bin_sz = binfile ? binfile->size : 0;
	// TODO: handle mode to print in json and r2 commands

	if (!binfile) return false;

	switch (mode) {
	case 'j':
		r_cons_printf("{\"name\":\"%s\",\"fd\":%d,\"id\":%d,\"objcnt\":%d,\"size\":%d,\"objs\":[",
			name, fd, id, obj_cnt, bin_sz);
		r_list_foreach (binfile->objs, iter, obj) {
			RBinInfo *info = obj->info;
			ut8 bits = info ? info->bits : 0;
			const char *arch = info ? info->arch : "unknown";
			r_cons_printf("{\"objid\":%d,\"arch\":\"%s\",\"bits\":%d,\"binoffset\":%"
					PFMT64d",\"objsize\":%"PFMT64d"}",
					obj->id, arch, bits, obj->boffset, obj->obj_size);
			if (iter->n) r_cons_printf (",");
		}
		r_cons_printf("]}");
		break;
	default:
		r_cons_printf("%d %s %d %d 0x%04x\n", fd, name, id, obj_cnt, bin_sz );
		r_list_foreach (binfile->objs, iter, obj) {
			RBinInfo *info = obj->info;
			ut8 bits = info ? info->bits : 0;
			const char *arch = info ? info->arch : "unknown";
			r_cons_printf("- %d %s %d 0x%04"PFMT64x" 0x%04"PFMT64x"\n",
					obj->id, arch, bits, obj->boffset, obj->obj_size );
		}
		break;
	}
	return true;
}

R_API int r_core_bin_list(RCore *core, int mode) {
	// list all binfiles and there objects and there archs
	int count = 0;
	RListIter *iter;
	RBinFile *binfile = NULL; //, *cur_bf = r_core_bin_cur (core) ;
	RBin *bin = core->bin;
	const RList *binfiles = bin ? bin->binfiles: NULL;

	if (!binfiles) return false;

	if (mode=='j') r_cons_printf("[");
	r_list_foreach (binfiles, iter, binfile) {
		r_core_bin_file_print (core, binfile, mode);
		if (iter->n && mode=='j') r_cons_printf(",");
	}
	if (mode=='j') r_cons_printf("]\n");
	//r_core_file_set_by_file (core, cur_cf);
	//r_core_bin_bind (core, cur_bf);
	return count;
}
