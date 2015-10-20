/* radare - LGPL - 2015 - pancake */

#include <r_types.h>
#include <r_util.h>
#include <r_lib.h>
#include <r_bin.h>
#include <string.h>

#if 0
Start	End	Length	Description
0x0	0x3	4	File offset to start of Text0
0x04	0x1b	24	File offsets for Text1..6
0x1c	0x47	44	File offsets for Data0..10
0x48	0x4B	4	Loading address for Text0
0x4C	0x8F	68	Loading addresses for Text1..6, Data0..10
0x90	0xD7	72	Section sizes for Text0..6, Data0..10
0xD8	0xDB	4	BSS address
0xDC	0xDF	4	BSS size
0xE0	0xE3	4	Entry point
0xE4	0xFF		padding
#endif

#define N_TEXT 7
#define N_DATA 11

typedef struct __attribute__((packed)) {
	ut32 text_paddr[N_TEXT];
	ut32 data_paddr[N_DATA];
	ut32 text_vaddr[N_TEXT];
	ut32 data_vaddr[N_DATA];
	ut32 text_size[N_TEXT];
	ut32 data_size[N_DATA];
	ut32 bss_addr;
	ut32 bss_size;
	ut32 entrypoint;
	ut32 padding[10];
	// 0x100 -- start of data section
} DolHeader;

static int check(RBinFile *arch);
static int check_bytes(const ut8 *buf, ut64 length);

static int check(RBinFile *arch) {
	const ut8 *bytes = arch ? r_buf_buffer (arch->buf) : NULL;
	ut64 sz = arch ? r_buf_size (arch->buf): 0;
	return check_bytes (bytes, sz);
}

static int check_bytes(const ut8 *buf, ut64 length) {
	if (!buf || length < 6) return false;
	return (!memcmp (buf, "\x00\x00\x01\x00\x00\x00", 6));
}

static void * load_bytes(RBinFile *arch, const ut8 *buf, ut64 sz, ut64 loadaddr, Sdb *sdb) {
	bool has_dol_extension = false;
	DolHeader * dol;
	char *lowername, *ext;
	if (!arch || sz < sizeof (DolHeader)) {
		return NULL;
	}
	dol = R_NEW0 (DolHeader);
	if (!dol) return NULL;
	lowername = strdup (arch->file);
	if (!lowername) {
		free (dol);
		return NULL;
	}
	r_str_case (lowername, 0);
	ext = strstr (lowername, ".dol");
	if (ext && ext[4] == 0) {
		has_dol_extension = true;
	}
	free (lowername);
	if (has_dol_extension) {
		r_buf_fread_at (arch->buf, 0, (void*)dol, "67I", 1);
		//r_buf_fread_at (arch->buf, 0, (void*)dol, "67i", 1);
		if (arch && arch->o && arch->o->bin_obj) {
			arch->o->bin_obj = dol;
		}
		return (void*)dol;
	}
	free (dol);
	return NULL;
}

static int load(RBinFile *arch) {
	const ut8 *bytes = arch ? r_buf_buffer (arch->buf) : NULL;
	ut64 sz = arch ? r_buf_size (arch->buf): 0;
	if (!arch || !arch->o)
		return false;
	arch->o->bin_obj = load_bytes (arch, bytes,
		sz, arch->o->loadaddr, arch->sdb);
	return check_bytes (bytes, sz);
}

static RList* sections(RBinFile *arch) {
	int i;
	RList *ret;
	RBinSection *s;
	DolHeader *dol;
	if (!arch || !arch->o || !arch->o->bin_obj)
		return NULL;
	dol = arch->o->bin_obj;
	if (!(ret = r_list_new ())) {
		return NULL;
	}

	/* text sections */
	for (i=0; i<N_TEXT; i++) {
		if (!dol->text_paddr[i] || !dol->text_vaddr[i])
			continue;
		s = R_NEW0 (RBinSection);
		snprintf (s->name, sizeof (s->name), "text_%d", i);
		s->paddr = dol->text_paddr[i];
		s->vaddr = dol->text_vaddr[i];
		s->size = dol->text_size[i];
		s->vsize = s->size;
		s->srwx = r_str_rwx ("mr-x");
		r_list_append (ret, s);
	}
	/* data sections */
	for (i=0; i<N_DATA; i++) {
		if (!dol->data_paddr[i] || !dol->data_vaddr[i])
			continue;
		s = R_NEW0 (RBinSection);
		snprintf (s->name, sizeof (s->name), "data_%d", i);
		s->paddr = dol->data_paddr[i];
		s->vaddr = dol->data_vaddr[i];
		s->size = dol->data_size[i];
		s->vsize = s->size;
		s->srwx = r_str_rwx ("mr--");
		r_list_append (ret, s);
	}
	/* bss section */
	s = R_NEW0 (RBinSection);
	strcpy (s->name, "bss");
	s->paddr = 0;
	s->vaddr = dol->bss_addr;
	s->size = dol->bss_size;
	s->vsize = s->size;
	s->srwx = r_str_rwx ("-rw-");
	r_list_append (ret, s);

	return ret;
}

static RList* entries(RBinFile *arch) {
	RList *ret;
	RBinAddr *addr;
	DolHeader *dol;
	if (!arch || !arch->o || !arch->o->bin_obj) {
		return NULL;
	}
	ret = r_list_new ();
	addr = R_NEW0 (RBinAddr);
	dol = arch->o->bin_obj;
	addr->vaddr = (ut64)dol->entrypoint;
	addr->paddr = addr->vaddr & 0xFFFF;
	r_list_append (ret, addr);
	return ret;
}

static RBinInfo* info(RBinFile *arch) {
	RBinInfo *ret = R_NEW0 (RBinInfo);
	if (!ret) return NULL;

	if (!arch || !arch->buf) {
		free (ret);
		return NULL;
	}
	ret->file = strdup (arch->file);
	ret->big_endian = true;
	ret->type = strdup ("ROM");
	ret->machine = strdup ("Nintendo Wii");
	ret->os = strdup ("wii-ios");
	ret->arch = strdup ("ppc");
	ret->has_va = true;
	ret->bits = 32;

	return ret;
}

static ut64 baddr(RBinFile *arch) {
	return 0x80b00000; // XXX
}

struct r_bin_plugin_t r_bin_plugin_dol = {
	.name = "dol",
	.desc = "Nintendo Dolphin binary format",
	.license = "BSD",
	.init = NULL,
	.fini = NULL,
	.get_sdb = NULL,
	.load = &load,
	.check = &check,
	.baddr = &baddr,
	.check_bytes = &check_bytes,
	.entries = &entries,
	.sections = &sections,
	.info = &info,
};

#ifndef CORELIB
struct r_lib_struct_t radare_plugin = {
	.type = R_LIB_TYPE_BIN,
	.data = &r_bin_plugin_dol,
	.version = R2_VERSION
};
#endif

