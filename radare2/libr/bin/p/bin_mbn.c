/* radare2 - LGPL - Copyright 2015 - pancake */

#include <r_types.h>
#include <r_util.h>
#include <r_lib.h>
#include <r_bin.h>

typedef struct sbl_header {
	ut32 load_index;
	ut32 version;    // (flash_partition_version) 3 = nand
	ut32 paddr;      // This + 40 is the start of the code in the file
	ut32 vaddr;	 // Where it's loaded in memory
	ut32 psize;      // code_size + signature_size + cert_chain_size
	ut32 code_pa;    // Only what's loaded to memory
	ut32 sign_va;
	ut32 sign_sz;
	ut32 cert_va;    // Max of 3 certs?
	ut32 cert_sz;
} SBLHDR;

static int check(RBinFile *arch);
static int check_bytes(const ut8 *buf, ut64 length);

// TODO avoid globals
static SBLHDR sb = {0};

static int check(RBinFile *arch) {
	const ut8 *bytes = arch ? r_buf_buffer (arch->buf) : NULL;
	ut64 sz = r_buf_size (arch->buf);
	return check_bytes (bytes, sz);
}

static int check_bytes(const ut8 *buf, ut64 bufsz) {
	if (buf && bufsz >= sizeof (SBLHDR)) {
		RBuffer *b = r_buf_new_with_pointers (buf, bufsz);
		int ret = r_buf_fread_at (b, 0, (ut8*)&sb, "10i", 1);
		r_buf_free (b);
		if (!ret) {
			return false;
		}
		if (sb.version != 3) { // NAND
			return false;
		}
		if (sb.paddr + sizeof (SBLHDR) > bufsz) { // NAND
			return false;
		}
		if (sb.vaddr < 0x100 || sb.psize > bufsz) { // NAND
			return false;
		}
		if (sb.cert_va < sb.vaddr) return false;
		if (sb.cert_sz >= 0xf0000) return false;
		if (sb.sign_va < sb.vaddr) return false;
		if (sb.sign_sz >= 0xf0000) return false;
		if (sb.load_index < 0x10 || sb.load_index > 0x40) return false; // should be 0x19 ?
#if 0
		eprintf ("V=%d\n", sb.version);
		eprintf ("PA=0x%08x sz=0x%x\n", sb.paddr, sb.psize);
		eprintf ("VA=0x%08x sz=0x%x\n", sb.vaddr, sb.psize);
		eprintf ("CODE=0x%08x\n", sb.code_pa + sb.vaddr+40);
		eprintf ("SIGN=0x%08x sz=0x%x\n", sb.sign_va, sb.sign_sz);
		if (sb.cert_sz > 0) {
			eprintf ("CERT=0x%08x sz=0x%x\n", sb.cert_va, sb.cert_sz);
		} else {
			eprintf ("No certificate found.\n");
		}
#endif
// TODO: Add more checks here
		return true;
	}
	return false;
}

static void * load_bytes(RBinFile *arch, const ut8 *buf, ut64 sz, ut64 loadaddr, Sdb *sdb){
	return (void*)(size_t)check_bytes (buf, sz);
}

static int load(RBinFile *arch) {
	return check(arch);
}

static int destroy (RBinFile *arch) {
	return true;
}

static ut64 baddr(RBinFile *arch) {
	return sb.vaddr; // XXX
}

static RList* entries(RBinFile *arch) {
	RList* ret;
	RBinAddr *ptr = NULL;

	if (!(ret = r_list_new ()))
		return NULL;
	ret->free = free;
	if ((ptr = R_NEW0 (RBinAddr))) {
		ptr->paddr = 40 + sb.code_pa;
		ptr->vaddr = 40 + sb.code_pa + sb.vaddr;
		r_list_append (ret, ptr);
	}
	return ret;
}

static RList* sections(RBinFile *arch) {
	RBinSection *ptr = NULL;
	RList *ret = NULL;
	int rc;

	if (!(ret = r_list_new ()))
		return NULL;
	ret->free = free;
	rc = r_buf_fread_at (arch->buf, 0, (ut8*)&sb, "10i", 1);
	if (!rc) return false;

	// add text segment
	if (!(ptr = R_NEW0 (RBinSection)))
		return ret;
	strncpy (ptr->name, "text", R_BIN_SIZEOF_STRINGS);
	ptr->size = sb.psize;
	ptr->vsize = sb.psize;
	ptr->paddr = sb.paddr + 40;
	ptr->vaddr = sb.vaddr;
	ptr->srwx = R_BIN_SCN_READABLE | R_BIN_SCN_EXECUTABLE | R_BIN_SCN_MAP; // r-x
	ptr->has_strings = true;
	r_list_append (ret, ptr);

	if (!(ptr = R_NEW0 (RBinSection)))
		return ret;
	strncpy (ptr->name, "sign", R_BIN_SIZEOF_STRINGS);
	ptr->size = sb.sign_sz;
	ptr->vsize = sb.sign_sz;
	ptr->paddr = sb.sign_va - sb.vaddr;
	ptr->vaddr = sb.sign_va;
	ptr->srwx = R_BIN_SCN_READABLE | R_BIN_SCN_MAP; // r--
	ptr->has_strings = true;
	r_list_append (ret, ptr);

	if (sb.cert_sz && sb.cert_va > sb.vaddr) {
		if (!(ptr = R_NEW0 (RBinSection)))
			return ret;
		strncpy (ptr->name, "cert", R_BIN_SIZEOF_STRINGS);
		ptr->size = sb.cert_sz;
		ptr->vsize = sb.cert_sz;
		ptr->paddr = sb.cert_va - sb.vaddr;
		ptr->vaddr = sb.cert_va;
		ptr->srwx = R_BIN_SCN_READABLE | R_BIN_SCN_MAP; // r--
		ptr->has_strings = true;
		r_list_append (ret, ptr);
	}
	return ret;
}

static RBinInfo* info(RBinFile *arch) {
	RBinInfo *ret = NULL;
	const int bits = 16;
	if ((ret = R_NEW0 (RBinInfo)) == NULL)
		return NULL;
	ret->file = strdup (arch->file);
	ret->bclass = strdup ("bootloader");
	ret->rclass = strdup ("mbn");
	ret->os = strdup ("MBN");
	ret->arch = strdup ("arm");
	ret->machine = strdup (ret->arch);
	ret->subsystem = strdup ("mbn");
	ret->type = strdup ("sbl"); // secondary boot loader
	ret->bits = bits;
	ret->has_va = true;
	ret->has_crypto = true; // must be false if there' no sign or cert sections
	ret->has_pi = false;
	ret->has_nx = false;
	ret->big_endian = false;
	ret->dbg_info = false;
	return ret;
}

static int size(RBinFile *arch) {
	return sizeof (SBLHDR) + sb.psize;
}

struct r_bin_plugin_t r_bin_plugin_mbn = {
	.name = "mbn",
	.desc = "MBN/SBL bootloader things",
	.license = "LGPL3",
	.minstrlen = 10,
	.load = &load,
	.load_bytes = &load_bytes,
	.size = &size,
	.destroy = &destroy,
	.check = &check,
	.check_bytes = &check_bytes,
	.baddr = &baddr,
	.entries = &entries,
	.sections = &sections,
	.info = &info,
};

#ifndef CORELIB
struct r_lib_struct_t radare_plugin = {
	.type = R_LIB_TYPE_BIN,
	.data = &r_bin_plugin_mbn,
	.version = R2_VERSION
};
#endif
