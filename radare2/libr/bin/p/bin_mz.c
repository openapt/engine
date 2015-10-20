/* radare - LGPL - Copyright 2015 nodepad */

#include <r_types.h>
#include <r_bin.h>
#include "mz/mz.h"

static Sdb * get_sdb(RBinObject *o) {
	const struct r_bin_mz_obj_t *bin;
	if (!o || !o->bin_obj) return NULL;
	bin = (struct r_bin_mz_obj_t *) o->bin_obj;
	if (bin && bin->kv) return bin->kv;
	return NULL;
}

static int check_bytes(const ut8 *buf, ut64 length) {
	unsigned int exth_offset;
	int ret = false;
	if (!buf)
		return false;
	if (length <= 0x3d)
		return false;
	if (!memcmp (buf, "MZ", 2) || !memcmp (buf, "ZM", 2))
	{
		ret = true;

		exth_offset = (buf[0x3c] | (buf[0x3d]<<8));
		if (length > exth_offset+2)
		{
			if (!memcmp (buf+exth_offset, "PE", 2) ||
				!memcmp (buf+exth_offset, "NE", 2) ||
				!memcmp (buf+exth_offset, "LE", 2) ||
				!memcmp (buf+exth_offset, "LX", 2) )
				ret = false;
		}
	}
	return ret;
}

static int check(RBinFile *arch) {
	const ut8 *bytes = arch ? r_buf_buffer (arch->buf) : NULL;
	const ut64 sz = arch ? r_buf_size (arch->buf): 0;
	return check_bytes (bytes, sz);
}

static void * load_bytes(RBinFile *arch, const ut8 *buf, ut64 sz,
		ut64 loadaddr, Sdb *sdb) {
	const struct r_bin_mz_obj_t *res = NULL;
	RBuffer *tbuf = NULL;
	if (!buf || sz == 0 || sz == UT64_MAX) return NULL;
	tbuf = r_buf_new ();
	r_buf_set_bytes (tbuf, buf, sz);
	res = r_bin_mz_new_buf (tbuf);
	if (res)
		sdb_ns_set (sdb, "info", res->kv);
	r_buf_free (tbuf);
	return (void *)res;
}

static int load(RBinFile *arch) {
	const void *res;
	const ut8 *bytes;
	ut64 sz;

	if (!arch || !arch->o)
		return false;

	bytes = r_buf_buffer (arch->buf);
	sz = r_buf_size (arch->buf);
	res = load_bytes (arch, bytes, sz, arch->o->loadaddr, arch->sdb);
	arch->o->bin_obj = (void *)res;
	return res != NULL;
}

static int destroy(RBinFile *arch) {
	r_bin_mz_free ((struct r_bin_mz_obj_t*)arch->o->bin_obj);
	return true;
}

static RList * entries(RBinFile *arch) {
	int entry;
	RList *res = NULL;
	RBinAddr *ptr = NULL;

	if (!(res = r_list_new ()))
		return NULL;
	res->free = free;

	entry = r_bin_mz_get_entrypoint (arch->o->bin_obj);

	if (entry >= 0) {
		if ((ptr = R_NEW (RBinAddr))) {
			ptr->paddr = (ut64) entry;
			ptr->vaddr = (ut64) entry;
			r_list_append (res, ptr);
		}
	}

	return res;
}

static RList * sections(RBinFile *arch) {
	RList *ret = NULL;
	RBinSection *ptr = NULL;
	const struct r_bin_mz_segment_t *segments = NULL;
	int i;
	if (!(ret = r_list_new ()))
		return NULL;
	ret->free = free;
	if (!(segments = r_bin_mz_get_segments (arch->o->bin_obj))){
		r_list_free (ret);
		return NULL;
	}
	for (i = 0; !segments[i].last; i++) {
		if (!(ptr = R_NEW0 (RBinSection))) {
			free ((void *)segments);
			r_list_free (ret);
			return NULL;
		}
		sprintf ((char*)ptr->name, "seg_%03d", i);
		ptr->size = segments[i].size;
		ptr->vsize = segments[i].size;
		ptr->paddr = segments[i].paddr;
		ptr->vaddr = segments[i].paddr;
		ptr->srwx = r_str_rwx ("mrwx");
		r_list_append (ret, ptr);
	}
	free ((void *)segments);
	return ret;
}

static RBinInfo * info(RBinFile *arch) {
	RBinInfo * const ret = R_NEW0 (RBinInfo);
	if (!ret) return NULL;
	ret->file = strdup (arch->file);
	ret->bclass = strdup ("MZ");
	ret->rclass = strdup ("mz");
	ret->os = strdup ("DOS");
	ret->arch = strdup ("x86");
	ret->machine = strdup ("i386");
	ret->type = strdup ("EXEC (Executable file)");
	ret->subsystem = strdup ("DOS");
	ret->rpath = NULL;
	ret->cpu = NULL;
	ret->guid = NULL;
	ret->debug_file_name = NULL;
	ret->bits = 16;
	ret->big_endian = false;
	ret->dbg_info = 0;
	ret->has_crypto = false;
	ret->has_canary = false;
	ret->has_nx = false;
	ret->has_pi = false;
	ret->has_va = false;

	return ret;
}

static RList * relocs(RBinFile *arch) {
	RList *ret = NULL;
	RBinReloc *rel = NULL;
	const struct r_bin_mz_reloc_t *relocs = NULL;
	int i;

	if (!arch || !arch->o || !arch->o->bin_obj)
		return NULL;
	if (!(ret = r_list_new ()))
		return NULL;

	ret->free = free;

	if (!(relocs = r_bin_mz_get_relocs (arch->o->bin_obj)))
		return ret;
	for (i = 0; !relocs[i].last; i++) {

		if (!(rel = R_NEW0 (RBinReloc))) {
			free ((void *)relocs);
			r_list_free (ret);
			return NULL;
		}
		rel->type = R_BIN_RELOC_16;
		rel->vaddr = relocs[i].paddr;
		rel->paddr = relocs[i].paddr;
		r_list_append (ret, rel);
	}
	free ((void *)relocs);
	return ret;
}

struct r_bin_plugin_t r_bin_plugin_mz = {
	.name = "mz",
	.desc = "MZ bin plugin",
	.license = "MIT",
	.init = NULL,
	.fini = NULL,
	.get_sdb = &get_sdb,
	.load = &load,
	.load_bytes = &load_bytes,
	.destroy = &destroy,
	.check = &check,
	.check_bytes = &check_bytes,
	.baddr = NULL,
	.boffset = NULL,
	.binsym = NULL,
	.entries = &entries,
	.sections = &sections,
	.symbols = NULL,
	.imports = NULL,
	.strings = NULL,
	.info = &info,
	.fields = NULL,
	.libs = NULL,
	.relocs = &relocs,
	.dbginfo = NULL,
	.write = NULL,
	.minstrlen = 4,
	.create = NULL,
	.get_vaddr = NULL
};

#ifndef CORELIB
struct r_lib_struct_t radare_plugin = {
	.type = R_LIB_TYPE_BIN,
	.data = &r_bin_plugin_mz,
	.version = R2_VERSION
};
#endif
