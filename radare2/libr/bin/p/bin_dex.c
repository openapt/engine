/* radare - LGPL - Copyright 2011-2015 - pancake */

#include <r_types.h>
#include <r_util.h>
#include <r_lib.h>
#include <r_bin.h>
#include "dex/dex.h"
#define r_hash_adler32 __adler32
#include "../../hash/adler32.c"

#define DEBUG_PRINTF 0

#if DEBUG_PRINTF
#define dprintf eprintf
#else
#define dprintf if (0)eprintf
#endif

static Sdb *mdb= NULL;

static int check(RBinFile *arch);
static int check_bytes(const ut8 *buf, ut64 length);

static Sdb* get_sdb (RBinObject *o) {
	if (!o || !o->bin_obj) return NULL;
	struct r_bin_dex_obj_t *bin = (struct r_bin_dex_obj_t *) o->bin_obj;
	if (bin->kv) return bin->kv;
	return NULL;
}

static void * load_bytes(RBinFile *arch, const ut8 *buf, ut64 sz, ut64 loadaddr, Sdb *sdb){
	void *res = NULL;
	RBuffer *tbuf = NULL;
	if (!buf || sz == 0 || sz == UT64_MAX) return NULL;
	tbuf = r_buf_new ();
	r_buf_set_bytes (tbuf, buf, sz);
	res = r_bin_dex_new_buf (tbuf);
	r_buf_free (tbuf);
	return res;
}

static int load(RBinFile *arch) {
	const ut8 *bytes = arch ? r_buf_buffer (arch->buf) : NULL;
	ut64 sz = arch ? r_buf_size (arch->buf): 0;

	if (!arch || !arch->o) return false;
	arch->o->bin_obj = load_bytes (arch, bytes, sz, arch->o->loadaddr, arch->sdb);
	return arch->o->bin_obj ? true: false;
}

static ut64 baddr(RBinFile *arch) {
	return 0;
}

static char *flagname (const char *class, const char *method) {
	int s_len;
	char *p, *str, *s;
	if (!class || !method)
		return NULL;
	s_len = strlen (class) + strlen (method)+10;
	s = malloc (s_len);
	if (!s) return NULL;
	str = s;
	p = (char*)r_str_lchr (class, '$');
	if (!p) p = (char *)r_str_lchr (class, '/');
	p = (char*)r_str_rchr (class, p, '/');
	if (p && *p) class = p+1;
	for (str=s; *class; class++) {
		switch (*class) {
		case '$':
		case ' ':
		case '/': *s++ = '_'; break;
		case ';': *s++ = '.'; break;
		default: *s++ = *class; break;
		}
	}
	for (*s++='.'; *method; method++) {
		switch (*method) {
		case '<': case '>':
		case '/': *s++ = '_'; break;
		case ';': *s++ = '.'; break;
		default: *s++ = *method; break;
		}
	}
	*s = 0;
	return str;
}

static int check(RBinFile *arch) {
	const ut8 *bytes = arch ? r_buf_buffer (arch->buf) : NULL;
	ut64 sz = arch ? r_buf_size (arch->buf): 0;
	return check_bytes (bytes, sz);
}

static int check_bytes(const ut8 *buf, ut64 length) {
	if (!buf || length < 8)
		return false;
	// Non-extended opcode dex file
	if (!memcmp (buf, "dex\n035\0", 8)) {
	        return true;
	}
	// Extended (jumnbo) opcode dex file, ICS+ only (sdk level 14+)
	if (!memcmp (buf, "dex\n036\0", 8))
	        return true;
	// M3 (Nov-Dec 07)
	if (!memcmp (buf, "dex\n009\0", 8))
	        return true;
	// M5 (Feb-Mar 08)
        if (!memcmp (buf, "dex\n009\0", 8))
	        return true;
	// Default fall through, should still be a dex file
	if (!memcmp (buf, "dex\n", 4))
                return true;
	return false;
}

static RBinInfo *info(RBinFile *arch) {
	RBinHash *h;
	RBinInfo *ret = R_NEW0 (RBinInfo);
	if (!ret) return NULL;
	ret->file = arch->file? strdup (arch->file): NULL;
	ret->type = strdup ("DEX CLASS");
	ret->has_va = false;
	ret->bclass = r_bin_dex_get_version (arch->o->bin_obj);
	ret->rclass = strdup ("class");
	ret->os = strdup ("linux");
	ret->subsystem = strdup ("any");
	ret->machine = strdup ("Dalvik VM");

	h = &ret->sum[0];
	h->type = "sha1";
	h->len = 20;
	h->addr = 12;
	h->from = 12;
	h->to = arch->buf->length-32;
	memcpy (h->buf, arch->buf->buf+12, 20);

	h = &ret->sum[1];
	h->type = "adler32";
	h->len = 4;
	h->addr = 0x8;
	h->from = 12;
	h->to = arch->buf->length-h->from;

	h = &ret->sum[2];
	h->type = 0;

	memcpy (h->buf, arch->buf->buf+8, 4);
	{
		ut32 *fc = (ut32 *)(arch->buf->buf + 8);
		ut32  cc = __adler32 (arch->buf->buf + h->from, h->to);
		//ut8 *fb = (ut8*)fc, *cb = (ut8*)&cc;
		if (*fc != cc) {
			dprintf ("# adler32 checksum doesn't match. Type this to fix it:\n");
			dprintf ("wx `#sha1 $s-32 @32` @12 ; wx `#adler32 $s-12 @12` @8\n");
		}
	}

	ret->arch = strdup ("dalvik");
	ret->lang = "java";
	ret->bits = 32;
	ret->big_endian = 0;
	ret->dbg_info = 0; //1 | 4 | 8; /* Stripped | LineNums | Syms */
	return ret;
}

static RList* strings (RBinFile *arch) {
	struct r_bin_dex_obj_t *bin = (struct r_bin_dex_obj_t *) arch->o->bin_obj;
	RBinString *ptr = NULL;
	RList *ret = NULL;
	int i, len;
	ut8 buf[6];

	if (!bin || !bin->strings)
		return NULL;
	if (bin->header.strings_size>bin->size) {
		bin->strings = NULL;
		return NULL;
	}
	if (!(ret = r_list_new ()))
		return NULL;
	ret->free = free;
	for (i = 0; i < bin->header.strings_size; i++) {
		if (!(ptr = R_NEW0 (RBinString)))
			break;
		r_buf_read_at (bin->b, bin->strings[i], (ut8*)&buf, 6);
		len = dex_read_uleb128 (buf);
		if (len>1 && len < R_BIN_SIZEOF_STRINGS) {
			r_buf_read_at (bin->b, bin->strings[i]+dex_uleb128_len (buf),
					(ut8*)&ptr->string, len);
			ptr->string[(int) len+1]='\0';
			ptr->vaddr = ptr->paddr = bin->strings[i];
			ptr->size = len;
			ptr->length = len;
			ptr->ordinal = i+1;
			r_list_append (ret, ptr);
		} else {
			dprintf ("dex_read_uleb128: invalid read\n");
			free (ptr);
		}
	}
	return ret;
}

#if 0
static inline ut32 getmethodoffset (struct r_bin_dex_obj_t *bin, int n, ut32 *size) {
	ut8 *buf, *map_end, *map;
	ut32 mapsz, off = 0L;
	int left;
	*size = 0;
	map = buf = r_buf_get_at (bin->b, bin->header.data_offset, &left);
	if (!map) return 0;
	for (map_end = map+bin->header.data_size; map<map_end;) {
		int num = map[0] + (map[1]<<8);
		int ninsn = map[12] + (map[13]<<8);
		map += 16; // skip header
		mapsz = ninsn%2? (ninsn+1)*2: ninsn*2;
		if (n == num) {
			*size = mapsz;
			off = bin->header.data_offset + (size_t)(map - buf);
			break;
		}
		map += mapsz;
	}
	return off;
}
#endif
static char *getstr (RBinDexObj *bin, int idx) {
	const ut8 buf[8], *buf2;
	ut64 len;
	int uleblen;
	if (idx<0 || idx >= bin->header.strings_size || !bin->strings)
		return NULL;
	r_buf_read_at (bin->b, bin->strings[idx], (ut8*)&buf, sizeof (buf));
	len = dex_read_uleb128 (buf);
	if (len<1)
		return NULL;
	buf2 = r_uleb128 (buf, sizeof (buf), &len);
	uleblen = (size_t)(buf2 - buf);
	if (len>0 && len < R_BIN_SIZEOF_STRINGS) {
		char *str = calloc (1, len+1);
		if (!str) return NULL;
		r_buf_read_at (bin->b, (bin->strings[idx])+uleblen,
				(ut8*)str, len); //uleblen);
		str[len] = 0;
		return str;
	}
	return NULL;
}

static char *get_string (RBinDexObj *bin, int cid, int idx) {
	char *c_name, *m_name, *res;
	if (idx<0)
		return NULL;
	if (idx<0 || idx>=bin->header.strings_size)
		return NULL;
	if (cid<0 || cid>=bin->header.strings_size)
		return NULL;
	c_name = getstr (bin, cid);
	m_name = getstr (bin, idx);
	if (c_name && *c_name==',') {
		res = r_str_newf ("%s", m_name);
	} else {
		if (c_name && m_name) {
			res = r_str_newf ("%s.%s", c_name, m_name);
		} else {
			res = r_str_newf ("UNKNOWN");
		}
	}
	free (c_name);
	free (m_name);
	return res;
}

/* TODO: check boundaries */
static char *dex_method_name (RBinDexObj *bin, int idx) {
	int cid, tid;
	if (idx<0 || idx>=bin->header.method_size)
		return NULL;
	cid = bin->methods[idx].class_id;
	tid = bin->methods[idx].name_id;
	if (cid<0 || cid >= bin->header.strings_size)
		return NULL;
	if (tid<0 || tid >= bin->header.strings_size)
		return NULL;
	return get_string (bin, cid, tid);
}

static char *dex_class_name_byid (RBinDexObj *bin, int cid) {
	int tid;
	if (!bin || !bin->types)
		return NULL;
	//cid = c->super_class;
	if (cid<0 || cid >= bin->header.types_size)
		return NULL;
	tid = bin->types [cid].descriptor_id;
	//int sid = bin->strings[tid];
	return get_string (bin, cid, tid);
}

static char *getClassName(const char *name) {
	const char *p;
	if (!name)
		return NULL;
	p = strstr (name, ".L");
	if (p) {
		char *q, *r = strdup (p+2);
		q = strchr (r, ';');
		if (q) *q = 0;
		return r;
	}
	return NULL;
}

static char *dex_class_name (RBinDexObj *bin, RBinDexClass *c) {
	int cid, tid;
	if (!bin || !c || !bin->types)
		return NULL;
	cid = c->class_id;
	//cid = c->super_class;
	if (cid<0 || cid >= bin->header.types_size)
		return NULL;
	tid = bin->types [cid].descriptor_id;
	//int sid = bin->strings[tid];
	return get_string (bin, cid, tid);
}

static char *dex_class_super_name (RBinDexObj *bin, RBinDexClass *c) {
	int cid, tid;
	if (!bin || !c || !bin->types)
		return NULL;
	cid = c->super_class;
	if (cid<0 || cid >= bin->header.types_size)
		return NULL;
	tid = bin->types [cid].descriptor_id;
	//int sid = bin->strings[tid];
	return get_string (bin, cid, tid);
}

static int *parse_class (RBinFile *binfile, struct r_bin_dex_obj_t *bin, struct dex_class_t *c, RBinClass *cls) {
	int i, *methods;
	char *name;
	ut64 SF, IF, DM, VM;
	const ut8 *p, *p_end;
	char *class_name;
	if (!c || !c->class_data_offset) {
		// no method here, just class definition
		//free (class_name);
		//free (super_name);
		return NULL;
	}
	class_name = dex_class_name (bin, c);
	if (!class_name) {
		return NULL;
	}
	methods = calloc (sizeof (ut32), bin->header.method_size);
	if (!methods) {
		free (class_name);
		return false;
	}
	dprintf ("  class_data_offset: %d\n", c->class_data_offset);
	p = r_buf_get_at (binfile->buf, c->class_data_offset, NULL);
	p_end = p + (binfile->buf->length - c->class_data_offset);
	/* data header */
	/* walk over class data items */
	p = r_uleb128 (p, p_end-p, &SF);
	p = r_uleb128 (p, p_end-p, &IF);
	p = r_uleb128 (p, p_end-p, &DM);
	p = r_uleb128 (p, p_end-p, &VM);
//eprintf ("METHODS %s %d\n", class_name, DM);
//eprintf ("SF %d IF %d DM %d VM %d\n", SF, IF, DM, VM);
	dprintf ("  static fields: %u\n", (ut32)SF);
	/* static fields */
	for (i=0; i<SF; i++) {
		ut64 FI, FA;
		p = r_uleb128 (p, p_end-p, &FI);
		p = r_uleb128 (p, p_end-p, &FA);
		dprintf ("    field_idx: %u\n", (ut32)FI);
// TODO: retrieve name of field here
// TODO: add comment or store that fcn var info in sdb directly
		dprintf ("    field access_flags: 0x%x\n", (ut32)FA);
	}
	/* instance fields */
	dprintf ("  instance fields: %u\n", (ut32)IF);
	for (i=0; i<IF; i++) {
		ut64 FI, FA;
		p = r_uleb128 (p, p_end-p, &FI);
		p = r_uleb128 (p, p_end-p, &FA);
		dprintf ("    field_idx: %u,\n", (ut32)FI);
		dprintf ("    field access_flags: %u,\n", (ut32)FA);
	}
	/* direct methods */
	dprintf ("  direct methods: %u\n", (ut32)DM);
#if 0
	// hardcoded DEX limit
	if (DM>=0xffff) {
		DM = 0xFFFF;
	}
#endif
	ut64 omi = 0;
	for (i=0; i<DM; i++) {
		char *method_name, *flag_name;
		ut64 MI, MA, MC;
		p = r_uleb128 (p, p_end-p, &MI);
		MI += omi;
		omi = MI;
		// the mi is diff
#if 0
		index into the method_ids list for the identity of this method (includes the name and descriptor), represented as a difference from the index of previous element in the list. The index of the first element in a list is represented directly. 
#endif
		p = r_uleb128 (p, p_end-p, &MA);
		p = r_uleb128 (p, p_end-p, &MC);

		if (MI<bin->header.method_size) methods[MI] = 1;
		if (MC>0 && bin->code_from>MC) bin->code_from = MC;
		if (MC>0 && bin->code_to<MC) bin->code_to = MC;

		method_name = dex_method_name (bin, MI);
		dprintf ("METHOD NAME %u\n", (ut32)MI);
		if (!method_name) method_name = strdup ("unknown");
		flag_name = flagname (class_name, method_name);
		dprintf ("f %s @ 0x%x\n", flag_name, (ut32)MC);
		dprintf ("    { name: %d %d %s,\n", (ut32)MC, (ut32)MI, method_name);
		dprintf ("      idx: %u,\n", (ut32)MI);
		dprintf ("      access_flags: 0x%x,\n", (ut32)MA);
		dprintf ("      code_offset: 0x%x },\n", (ut32)MC);
		/* add symbol */
		if (flag_name && *flag_name) {
			RBinSymbol *sym = R_NEW0 (RBinSymbol);
			strncpy (sym->name, flag_name, sizeof (sym->name)-1);
			strcpy (sym->type, "FUNC");
			sym->paddr = sym->vaddr = MC;
			if (MC>0) { /* avoid methods at 0 paddr */
#if 0
// TODO: use sdb+pf to show method header
ut16 regsz;
ut16 ins_size
ut16 outs_size
ut16 tries_size
ut32 debug_info_off
ut32 insns_size
ut16[insn_size] insns;
ut16 padding = 0
try_item[tries_size] tries
encoded_catch_handler_list handlers
#endif
				sym->paddr += 0x10;
				r_list_append (bin->methods_list, sym);
				if (cls) {
					if (!cls->methods) {
						cls->methods = r_list_new ();
					}
					r_list_append (cls->methods, sym);
				}
				/* cache in sdb */
				if (!mdb) {
					mdb = sdb_new0 ();
				}
				sdb_num_set (mdb, sdb_fmt(0, "method.%d", MI), sym->paddr, 0);
			} else {
				//r_list_append (bin->methods_list, sym);
				// XXX memleak sym
				free (sym);
			}
		}
		free (method_name);
		free (flag_name);
	}
	/* virtual methods */
	dprintf ("  virtual methods: %u\n", (ut32)VM);
	for (i=0; i<VM; i++) {
		ut64 MI, MA, MC;
		p = r_uleb128 (p, p_end-p, &MI);
		p = r_uleb128 (p, p_end-p, &MA);
		p = r_uleb128 (p, p_end-p, &MC);

		if (MI<bin->header.method_size) methods[MI] = 1;
		if (MC>0 && bin->code_from>MC) bin->code_from = MC;
		if (MC>0 && bin->code_to<MC) bin->code_to = MC;

		name = dex_method_name (bin, MI);
		dprintf ("    method name: %s\n", name);
		dprintf ("    method_idx: %u\n", (ut32)MI);
		dprintf ("    method access_flags: %u\n", (ut32)MA);
		dprintf ("    method code_offset: %u\n", (ut32)MC);
		free (name);
	}
	free (class_name);
	return methods;
}

static int dex_loadcode(RBinFile *arch, RBinDexObj *bin) {
	int i;
	int *methods = NULL;

	// doublecheck??
	if (!bin || bin->methods_list)
		return false;
	bin->code_from = UT64_MAX;
	bin->code_to = 0;
	bin->methods_list = r_list_new ();
	bin->methods_list->free = free;
	bin->imports_list = r_list_new ();
	bin->imports_list->free = free;

	if (bin->header.method_size>bin->size) {
		bin->header.method_size = 0;
		return false;
	}

	/* WrapDown the header sizes to avoid huge allocations */
	bin->header.method_size = R_MIN (bin->header.method_size, bin->size);
	bin->header.class_size = R_MIN (bin->header.class_size, bin->size);
	bin->header.strings_size = R_MIN (bin->header.strings_size, bin->size);

	if (bin->header.strings_size > bin->size) {
		eprintf ("Invalid strings size\n");
		return false;
	}

	dprintf ("Walking %d classes\n", bin->header.class_size);
	if (bin->classes)
	for (i=0; i<bin->header.class_size; i++) {
		char *super_name, *class_name;
		struct dex_class_t *c = &bin->classes[i];
		class_name = dex_class_name (bin, c);
		super_name = dex_class_super_name (bin, c);
		dprintf ("{\n");
		dprintf ("  class: 0x%x,\n", c->class_id); // indexed by ordinal
		dprintf ("  super: \"%s\",\n", super_name); // indexed by name
		dprintf ("  name: \"%s\",\n", class_name);
		dprintf ("  methods: [\n");
// sdb_queryf ("(-1)classes=%s", class_name)
// sdb_queryf ("class.%s.super=%s", super_name)
// sdb_queryf ("class.%s.methods=%d", class_name, DM);
#if 0
		if (c->class_data_offset == 0) {
			eprintf ("Skip class\n");
			continue;
		}
#endif
		free (methods);
		methods = parse_class (arch, bin, c, NULL);
		dprintf ("  ],\n");
		dprintf ("},");
		free (class_name);
		free (super_name);
	}
	if (methods) {
		dprintf ("imports: \n");
		for (i = 0; i<bin->header.method_size; i++) {
			//RBinDexMethod *method = &bin->methods[i];
			if (!methods[i]) {
				char *method_name = dex_method_name (bin, i);
				dprintf ("import %d (%s)\n", i, method_name);
				if (method_name && *method_name) {
					RBinSymbol *sym = R_NEW0 (RBinSymbol);
					strncpy (sym->name, method_name, R_BIN_SIZEOF_STRINGS);
					strcpy (sym->type, "FUNC");
					sym->paddr = sym->vaddr = 0; // UNKNOWN
					r_list_append (bin->imports_list, sym);
				}
				free (method_name);
			}
		}
		free (methods);
	}
	return true;
}

static RList* imports (RBinFile *arch) {
	RBinDexObj *bin = (RBinDexObj*) arch->o->bin_obj;
	if (!bin) {
		return NULL;
	}
	if (bin && bin->imports_list) {
		return bin->imports_list;
	}
	dex_loadcode (arch, bin);
	return bin->imports_list;
#if 0
	struct r_bin_dex_obj_t *bin = (struct r_bin_dex_obj_t *) arch->o->bin_obj;
	int i;
	RList *ret = NULL;
	RBinImport *ptr;

	if (!(ret = r_list_new ()))
		return NULL;
	ret->free = free;
	dprintf ("Importing %d methods... \n", bin->header.method_size);
	for (i = 0; i<bin->header.method_size; i++) {
		if (!(ptr = R_NEW (RBinImport)))
			break;
		char *methodname = get_string (bin, bin->methods[i].name_id);
		char *classname = get_string (bin, bin->methods[i].class_id);
		//char *typename = get_string (bin, bin->methods[i].type_id);
dprintf ("----> %d\n", bin->methods[i].name_id);

		if (!methodname) {
			dprintf ("string index out of range\n");
			break;
		}
		snprintf (ptr->name, sizeof (ptr->name), "import.%s.%s",
				classname, methodname);
		ptr->ordinal = i+1;
		ptr->size = 0;
		ptr->vaddr = ptr->offset = getmethodoffset (bin,
			(int)ptr->ordinal, (ut32*)&ptr->size);
dprintf ("____%s__%s____  (%d)  %llx\n", classname,
	methodname, bin->methods[i].name_id, ptr->vaddr);
free (classname);
free (methodname);
		//strncpy (ptr->forwarder, "NONE", R_BIN_SIZEOF_STRINGS);
		strncpy (ptr->bind, "NONE", R_BIN_SIZEOF_STRINGS);
		if (ptr->vaddr) {
			free (ptr);
			continue;
		}
		strncpy (ptr->type, "IMPORT", R_BIN_SIZEOF_STRINGS);
		r_list_append (ret, ptr);
	}
	dprintf ("Done\n");
	return ret;
#endif
}

static RList* methods (RBinFile *arch) {
	RBinDexObj *bin;
	if (!arch || !arch->o || !arch->o->bin_obj)
		return NULL;
	bin = (RBinDexObj*) arch->o->bin_obj;
	if (!bin->methods_list)
		dex_loadcode (arch, bin);
	return bin->methods_list;
}

// wtf?
static void __r_bin_class_free(RBinClass *p) {
	r_bin_class_free (p);
}

static RList* classes (RBinFile *arch) {
	struct r_bin_dex_obj_t *bin;
	struct dex_class_t entry;
	const int len = 100;
	RList *ret = NULL;
	int i, class_index = 0;
	char *name = NULL;
	RBinClass *class;
	if (!arch || !arch->o || !arch->o->bin_obj)
		return NULL;

	bin = (struct r_bin_dex_obj_t *) arch->o->bin_obj;
	if (bin->header.class_size>bin->size) {
		eprintf ("Too many classes %d\n", bin->header.class_size);
		return NULL;
	}
	if (!(ret = r_list_new ()))
		return NULL;
	ret->free = (RListFree)__r_bin_class_free;
	for (i = 0; i < bin->header.class_size; i++) {
		ut64 class_addr = (ut64) bin->header.class_offset \
			+ (sizeof (struct dex_class_t)*i);
		// ETOOSLOW
		r_buf_read_at (bin->b, class_addr, (ut8*)&entry,
			sizeof (struct dex_class_t));
		// TODO: implement sections.. each section specifies a class boundary
{
		free (name);
		name = malloc (len);
		if (!name) {
			dprintf ("error malloc string length %d\n", len);
			break;
		}
		// lazy check
		if (!bin->strings) {
			// no bin->strings found
			break;
		}
		if (entry.source_file >= bin->size) {
			continue;
		}
		// unsigned if (entry.source_file<0 || entry.source_file >= bin->header.strings_size)
		if (entry.source_file >= bin->header.strings_size)
			continue;
		r_buf_read_at (bin->b, bin->strings[entry.source_file],
				(ut8*)name, len);
		//snprintf (ptr->name, sizeof (ptr->name), "field.%s.%d", name, i);
		class = R_NEW0 (RBinClass);
		// get source file name (ClassName.java)
		// TODO: use RConstr here
		//class->name = strdup (name[0]<0x41? name+1: name); 
		class->name = dex_class_name_byid (bin, entry.class_id);
		// find reference to this class instance
		char *cn = getClassName(class->name);
		if (cn) {
			free (class->name);
			class->index = class_index++;
			class->addr = entry.class_id + bin->header.class_offset;
			class->name = cn;
			//class->addr = class_addr;

		int *methods = parse_class (arch, bin, &entry, class);
		free (methods);

			r_list_append (ret, class);
			dprintf ("class.%s=%d\n", name[0]==12?name+1:name, entry.class_id);
			dprintf ("# access_flags = %x;\n", entry.access_flags);
			dprintf ("# super_class = %d;\n", entry.super_class);
			dprintf ("# interfaces_offset = %08x;\n", entry.interfaces_offset);
			//dprintf ("ut32 source_file = %08x;\n", entry.source_file);
			dprintf ("# anotations_offset = %08x;\n", entry.anotations_offset);
			dprintf ("# class_data_offset = %08x;\n", entry.class_data_offset);
			dprintf ("# static_values_offset = %08x;\n\n", entry.static_values_offset);
		} else {
			free (class->name);
			free (class);
		}
	}
	}
	free (name);
	return ret;
}

static int already_entry (RList *entries, ut64 vaddr) {
	RBinAddr *e;
	RListIter *iter;
	r_list_foreach (entries, iter, e) {
		if (e->vaddr == vaddr)
			return 1;
	}
	return 0;
}

static RList* entries(RBinFile *arch) {
	RListIter *iter;
	RBinDexObj *bin;
	RList *ret;
	RBinAddr *ptr;
	RBinSymbol *m;

	if (!arch || !arch->o || !arch->o->bin_obj)
		return NULL;

	bin = (RBinDexObj*) arch->o->bin_obj;
	ret = r_list_new ();
	ptr = R_NEW0 (RBinAddr);

	if (!bin->methods_list) {
		dex_loadcode (arch, bin);
	}
	// XXX: entry + main???
	r_list_foreach (bin->methods_list, iter, m) {
		if (strlen (m->name)>=4 && !strcmp (m->name+strlen (m->name)-4, "main")) {
			dprintf ("ENTRY -> %s\n", m->name);
			ptr->paddr = ptr->vaddr = m->paddr;
			if (!already_entry (ret, ptr->vaddr)) {
				r_list_append (ret, ptr);
				ptr = R_NEW0 (RBinAddr);
				if (!ptr) break;
			}
		}
	}
	if (ptr && r_list_empty (ret)) {
		ptr->paddr = ptr->vaddr = bin->code_from;
		if (!already_entry (ret, ptr->vaddr)) {
			r_list_append (ret, ptr);
		}
	}
	return ret;
}

static ut64 offset_of_method_idx(RBinFile *arch, struct r_bin_dex_obj_t *dex, int idx) {
	int off = dex->header.method_offset +idx;
	//(sizeof (struct dex_method_t)*idx);
	//const char *name = dex_method_name (dex, idx);
	//eprintf ("idx=%d off=%d (%s)\n", idx, off, name);
	//off = sdb_num_get (mdb, name, NULL);
	off = sdb_num_get (mdb, sdb_fmt(0, "method.%d", idx), 0);
	//p = r_uleb128 (p, p_end-p, &MI);
	// READ CODE
	return off;
}

//TODO must return ut64 imho
static int getoffset (RBinFile *arch, int type, int idx) {
	struct r_bin_dex_obj_t *dex = arch->o->bin_obj;
	switch (type) {
	case 'm': // methods
		//if (dex->header.method_size > idx)
		//	return offset_of_method_idx (arch, dex, idx);
		return offset_of_method_idx (arch, dex, idx);
		break;
	case 'c': // class
		if (dex->header.class_size > idx) {
			int off = dex->header.class_offset +idx;
			//(sizeof (struct dex_class_t)*idx);
			//const char *name = dex_class_name_byid (dex, idx);
			//eprintf ("idx=%d off=%d (%s)\n", idx, off, name);
			//p = r_uleb128 (p, p_end-p, &MI);
			return off;
		}
		break;
	case 'f': // fields
		if (dex->header.fields_size > idx)
			return dex->header.fields_offset +
				(sizeof (struct dex_field_t)*idx);
		break;
	case 'o': // objects
		break;
	case 's': // strings
		if (dex->header.strings_size > idx)
			return dex->strings[idx];
		break;
	case 't': // things
		break;
	}
	return -1;
}

static RList* sections(RBinFile *arch) {
	struct r_bin_dex_obj_t *bin = arch->o->bin_obj;
	RList *ml = methods (arch);
	RBinSection *ptr = NULL;
	int ns, fsymsz = 0;
	RList *ret = NULL;
	RListIter *iter;
	RBinSymbol *m;
	int fsym = 0;

	r_list_foreach (ml, iter, m) {
		if (fsym == 0 || m->paddr<fsym)
			fsym = m->paddr;
		ns = m->paddr + m->size;
		if (ns > arch->buf->length)
			continue;
		if (ns>fsymsz)
			fsymsz = ns;
	}
	if (fsym == 0)
		return NULL;
	if (!(ret = r_list_new ()))
		return NULL;
	ret->free = free;

	if ((ptr = R_NEW0 (RBinSection))) {
		strcpy (ptr->name, "header");
		ptr->size = ptr->vsize = sizeof (struct dex_header_t);
		ptr->paddr= ptr->vaddr = 0;
		ptr->srwx = R_BIN_SCN_READABLE | R_BIN_SCN_MAP;
		r_list_append (ret, ptr);
	}
	if ((ptr = R_NEW0 (RBinSection))) {
		strcpy (ptr->name, "constpool");
		//ptr->size = ptr->vsize = fsym;
		ptr->paddr= ptr->vaddr = sizeof (struct dex_header_t);
		ptr->size = bin->code_from - ptr->vaddr; // fix size
		ptr->srwx = R_BIN_SCN_READABLE | R_BIN_SCN_MAP;
		r_list_append (ret, ptr);
	}
	if ((ptr = R_NEW0 (RBinSection))) {
		strcpy (ptr->name, "code");
		ptr->vaddr = ptr->paddr = bin->code_from; //ptr->vaddr = fsym;
		ptr->size = bin->code_to - ptr->paddr;
		ptr->srwx = R_BIN_SCN_READABLE | R_BIN_SCN_EXECUTABLE | R_BIN_SCN_MAP;
		r_list_append (ret, ptr);
	}
	if ((ptr = R_NEW0 (RBinSection))) {
		//ut64 sz = arch ? r_buf_size (arch->buf): 0;
		strcpy (ptr->name, "data");
		ptr->paddr = ptr->vaddr = fsymsz+fsym;
		if (ptr->vaddr > arch->buf->length) {
			ptr->paddr = ptr->vaddr = bin->code_to;
			ptr->size = ptr->vsize = arch->buf->length - ptr->vaddr;
		} else {
			ptr->size = ptr->vsize = arch->buf->length - ptr->vaddr;
			// hacky workaround
			dprintf ("Hack\n");
			//ptr->size = ptr->vsize = 1024;
		}
		ptr->srwx = R_BIN_SCN_READABLE | R_BIN_SCN_MAP; //|2;
		r_list_append (ret, ptr);
	}
	return ret;
}

static int size(RBinFile *arch) {
	int ret;
	ut32 off = 0, len = 0;
	ret = r_buf_fread_at (arch->buf, 100, (ut8*)&off, "i", 1);
	if (ret != 4) return 0;
	ret = r_buf_fread_at (arch->buf, 104, (ut8*)&len, "i", 1);
	if (ret != 4) return 0;
	return off+len + 0x20;
}
struct r_bin_plugin_t r_bin_plugin_dex = {
	.name = "dex",
	.desc = "dex format bin plugin",
	.license = "LGPL3",
	.init = NULL,
	.fini = NULL,
	.get_sdb = &get_sdb,
	.load = &load,
	.load_bytes = &load_bytes,
	.destroy = NULL,
	.check = &check,
	.check_bytes = &check_bytes,
	.baddr = &baddr,
	.boffset = NULL,
	.binsym = NULL,
	.entries = entries,
	.classes = classes,
	.sections = sections,
	.symbols = methods,
	.imports = imports,
	.strings = strings,
	.info = &info,
	.fields = NULL,
	.libs = NULL,
	.relocs = NULL,
	.dbginfo = NULL,
	.size = &size,
	.write = NULL,
	.get_offset = &getoffset
};

#ifndef CORELIB
struct r_lib_struct_t radare_plugin = {
	.type = R_LIB_TYPE_BIN,
	.data = &r_bin_plugin_dex,
	.version = R2_VERSION
};
#endif
