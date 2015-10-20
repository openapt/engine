/* radare - LGPL - Copyright 2015 - inisider */

#include <r_bin.h>

static int is_cxx_symbol (const char *name) {
	if (*name == '?')
		return 1;
	return 0;
}

R_API int r_bin_lang_msvc(RBinFile *binfile) {
	RBinObject *o = binfile ? binfile->o : NULL;
	RBinInfo *info = o ? o->info : NULL;
	RBinSymbol *sym;
	RListIter *iter;
	_Bool hascxx = false;
	if (info) {
		r_list_foreach (o->symbols, iter, sym) {
			if (is_cxx_symbol (sym->name)) {
				hascxx = true;
				break;
			}
		}
		if (hascxx)
			info->lang = "msvc";
	}
	return (int)hascxx;
}
