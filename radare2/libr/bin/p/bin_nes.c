/* radare - LGPL3 - 2015 - maijin */

#include <r_bin.h>
#include "nes/nes_specs.h"

static int check(RBinFile *arch);
static int check_bytes(const ut8 *buf, ut64 length);

static void * load_bytes(RBinFile *arch, const ut8 *buf, ut64 sz, ut64 loadaddr, Sdb *sdb){
	check_bytes (buf, sz);
	return R_NOTNULL;
}

static int check(RBinFile *arch) {
	const ut8 *bytes = arch ? r_buf_buffer (arch->buf) : NULL;
	ut64 sz = arch ? r_buf_size (arch->buf): 0;
	return check_bytes (bytes, sz);
}

static int check_bytes(const ut8 *buf, ut64 length) {
	if (!buf || length < 4) return false;
	return (!memcmp (buf, INES_MAGIC, 4));
}

static RBinInfo* info(RBinFile *arch) {
	RBinInfo *ret = NULL;
	ines_hdr ihdr;
	memset (&ihdr, 0, INES_HDR_SIZE);
	int reat = r_buf_read_at (arch->buf, 0, (ut8*)&ihdr, INES_HDR_SIZE);
	if (reat != INES_HDR_SIZE) {
		eprintf ("Truncated Header\n");
		return NULL;
	}
	if (!(ret = R_NEW0 (RBinInfo)))
		return NULL;
	ret->file = strdup (arch->file);
	ret->type = strdup ("ROM");
	ret->machine = strdup ("Nintendo NES");
	ret->os = strdup ("nes");
	ret->arch = strdup ("6502");
	ret->bits = 8;
	ret->has_va = 1;
	return ret;
}

static RList* symbols(RBinFile *arch) {
	RList *ret = NULL;
	RBinSymbol *ptr[3];
	if (!(ret = r_list_new()))
		return NULL;
	ret->free = free;
	if (!(ptr[0] = R_NEW0 (RBinSymbol)))
		return ret;
	strncpy (ptr[0]->name, "NMI_VECTOR_START_ADDRESS", R_BIN_SIZEOF_STRINGS);
	ptr[0]->vaddr = NMI_VECTOR_START_ADDRESS;
	ptr[0]->size = 2;
	ptr[0]->ordinal = 0;
	r_list_append (ret, ptr[0]);
	if (!(ptr[1] = R_NEW0 (RBinSymbol)))
		return ret;
	strncpy (ptr[1]->name, "RESET_VECTOR_START_ADDRESS", R_BIN_SIZEOF_STRINGS);
	ptr[1]->vaddr = RESET_VECTOR_START_ADDRESS;
	ptr[1]->size = 2;
	ptr[1]->ordinal = 1;
	r_list_append (ret, ptr[1]);
	if (!(ptr[2] = R_NEW0 (RBinSymbol)))
		return ret;
	strncpy (ptr[2]->name, "IRQ_VECTOR_START_ADDRESS", R_BIN_SIZEOF_STRINGS);
	ptr[2]->vaddr = IRQ_VECTOR_START_ADDRESS;
	ptr[2]->size = 2;
	ptr[2]->ordinal = 2;
	r_list_append (ret, ptr[2]);
	return ret;
}

static RList* sections(RBinFile *arch) {
	ines_hdr ihdr;
	memset (&ihdr, 0, INES_HDR_SIZE);
	int reat = r_buf_read_at (arch->buf, 0, (ut8*)&ihdr, INES_HDR_SIZE);
	if (reat != INES_HDR_SIZE) {
		eprintf ("Truncated Header\n");
		return NULL;
	}
	RList *ret = NULL;
	RBinSection *ptr = NULL;
	if (!(ret = r_list_new ()))
		return NULL;
	if (!(ptr = R_NEW0 (RBinSection)))
		return ret;
	strcpy (ptr->name, "ROM");
	ptr->paddr = INES_HDR_SIZE;
	ptr->size = ihdr.prg_page_count_16k * PRG_PAGE_SIZE;
	ptr->vaddr = ROM_START_ADDRESS;
	ptr->vsize = ROM_SIZE;
	ptr->srwx = R_BIN_SCN_MAP;
	r_list_append (ret, ptr);
	return ret;
}

static RList *mem (RBinFile *arch) {
	RList *ret;
	RBinMem *m, *n;
	if (!(ret = r_list_new()))
		return NULL;
	ret->free = free;
	if (!(m = R_NEW0 (RBinMem))) {
		r_list_free (ret);
		return NULL;
	}
	strncpy (m->name, "RAM", R_BIN_SIZEOF_STRINGS);
	m->addr = RAM_START_ADDRESS;
	m->size = RAM_SIZE;
	m->perms = r_str_rwx ("rwx");
	r_list_append (ret, m);
	if (!(n = R_NEW0 (RBinMem))) {
		r_list_free (m->mirrors);
		m->mirrors = NULL;
		return ret;
	}
	strncpy (n->name, "RAM_MIRROR_2", R_BIN_SIZEOF_STRINGS);
	n->addr = RAM_MIRROR_2_ADDRESS;
	n->size = RAM_MIRROR_2_SIZE;
	n->perms = r_str_rwx ("rwx");
	r_list_append (m->mirrors, n);
	if (!(n = R_NEW0 (RBinMem))) {
		r_list_free (m->mirrors);
		m->mirrors = NULL;
		return ret;
	}
	strncpy (n->name, "RAM_MIRROR_3", R_BIN_SIZEOF_STRINGS);
	n->addr = RAM_MIRROR_3_ADDRESS;
	n->size = RAM_MIRROR_3_SIZE;
	n->perms = r_str_rwx ("rwx");
	r_list_append (m->mirrors, n);
	if (!(m = R_NEW0 (RBinMem))) {
		r_list_free (ret);
		return NULL;
	}
	strncpy (m->name, "PPU_REG", R_BIN_SIZEOF_STRINGS);
	m->addr = PPU_REG_ADDRESS;
	m->size = PPU_REG_SIZE;
	m->perms = r_str_rwx ("rwx");
	r_list_append (ret, m);
	int i;
	for (i = 1; i < 1024; i++) {
		if (!(n = R_NEW0 (RBinMem))) {
			r_list_free (m->mirrors);
			m->mirrors = NULL;
			return ret;
		}
		strncpy (m->name, "PPU_REG_MIRROR_", R_BIN_SIZEOF_STRINGS);
		sprintf(m->name, "%d",i);
		m->addr = PPU_REG_ADDRESS+i*PPU_REG_SIZE;
		m->size = PPU_REG_SIZE;
		m->perms = r_str_rwx ("rwx");
		r_list_append (m->mirrors, n);
	}
	if (!(m = R_NEW0 (RBinMem))) {
		r_list_free (ret);
		return NULL;
	}
	strncpy (m->name, "APU_AND_IOREGS", R_BIN_SIZEOF_STRINGS);
	m->addr = APU_AND_IOREGS_START_ADDRESS;
	m->size = APU_AND_IOREGS_SIZE;
	m->perms = r_str_rwx ("rwx");
	r_list_append (ret, m);
	if (!(m = R_NEW0 (RBinMem))) {
		r_list_free (ret);
		return NULL;
	}
	strncpy (m->name, "SRAM", R_BIN_SIZEOF_STRINGS);
	m->addr = SRAM_START_ADDRESS;
	m->size = SRAM_SIZE;
	m->perms = r_str_rwx ("rwx");
	r_list_append (ret, m);

	return ret;
}

static RList* entries(RBinFile *arch) { //Should be 3 offsets pointed by NMI, RESET, IRQ after mapping && default = 1st CHR
	RList *ret;
	RBinAddr *ptr = NULL;
	if (!(ret = r_list_new ()))
		return NULL;
	if (!(ptr = R_NEW0 (RBinAddr)))
		return ret;
	ptr->paddr = INES_HDR_SIZE;
	ptr->vaddr = ROM_START_ADDRESS;
	r_list_append (ret, ptr);
	return ret;
}

struct r_bin_plugin_t r_bin_plugin_nes = {
	.name = "nes",
	.desc = "NES",
	.license = "LGPL3",
	.load_bytes = &load_bytes,
	.check = &check,
	.check_bytes = &check_bytes,
	.entries = &entries,
	.sections = sections,
	.symbols = &symbols,
	.info = &info,
	.mem = &mem,
};

#ifndef CORELIB
struct r_lib_struct_t radare_plugin = {
	.type = R_LIB_TYPE_BIN,
	.data = &r_bin_plugin_nes,
	.version = R2_VERSION
};
#endif
