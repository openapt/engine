/* radare - LGPL - Copyright 2015 - pancake */

#include "r_io.h"
#include "r_lib.h"
#include "r_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

typedef struct {
	int fd;
	RBuffer *buf;
	ut64 offset;
} RIOSparse;

#define RIOSPARSE_FD(x) (((RIOSparse*)x->data)->fd)
#define RIOSPARSE_BUF(x) (((RIOSparse*)x->data)->buf)
#define RIOSPARSE_OFF(x) (((RIOSparse*)x->data)->offset)

static int __write(RIO *io, RIODesc *fd, const ut8 *buf, int count) {
	ut64 o;
	RBuffer *b;
	if (fd == NULL || fd->data == NULL)
		return -1;
	b = RIOSPARSE_BUF(fd);
	o = RIOSPARSE_OFF(fd);
	return r_buf_write_at (b, o, buf, count);
}

static int __read(RIO *io, RIODesc *fd, ut8 *buf, int count) {
	ut64 o;
	RBuffer *b;
	if (fd == NULL || fd->data == NULL)
		return -1;
	b = RIOSPARSE_BUF(fd);
	o = RIOSPARSE_OFF(fd);
	(void)r_buf_read_at (b, o, buf, count);
	return count;
}

static int __close(RIODesc *fd) {
	RIOSparse *riom;
	if (fd == NULL || fd->data == NULL)
		return -1;
	riom = fd->data;
	free (riom->buf);
	riom->buf = NULL;
	free (fd->data);
	fd->data = NULL;
	fd->state = R_IO_DESC_TYPE_CLOSED;
	return 0;
}

static ut64 __lseek(RIO* io, RIODesc *fd, ut64 offset, int whence) {
	RBuffer *b;
	ut64 r_offset = offset;
	if (!fd->data)
		return offset;
	b = RIOSPARSE_BUF(fd);
	r_offset = r_buf_seek (b, offset, whence);
	//if (r_offset != UT64_MAX)
	RIOSPARSE_OFF (fd) = r_offset;
	return r_offset;
}

static int __plugin_open(struct r_io_t *io, const char *pathname, ut8 many) {
	return (!strncmp (pathname, "sparse://", 9));
}

static RIODesc *__open(RIO *io, const char *pathname, int rw, int mode) {
	if (__plugin_open (io, pathname,0)) {
		RIOSparse *mal = R_NEW0 (RIOSparse);
		mal->fd = -2; /* causes r_io_desc_new() to set the correct fd */
		int size = (int)r_num_math (NULL, pathname+9);
		mal->buf = r_buf_new_sparse ();
		if (size>0) {
			ut8 *data = malloc (size);
			if (!data) {
				eprintf ("Cannot allocate (%s) %d bytes\n",
					pathname+9, size);
				mal->offset = 0;
			} else {
				memset (data, 0x00, size);
				r_buf_write_at (mal->buf, 0, data, size);
				free (data);
			}
		}
		if (mal->buf) {
			RETURN_IO_DESC_NEW (&r_io_plugin_sparse,
				mal->fd, pathname, rw, mode, mal);
		}
		r_buf_free (mal->buf);
		free (mal);
	}
	return NULL;
}

struct r_io_plugin_t r_io_plugin_sparse = {
	.name = "sparse",
	.desc = "sparse buffer allocation (sparse://1024 sparse://)",
	.license = "LGPL3",
	.open = __open,
	.close = __close,
	.read = __read,
	.plugin_open = __plugin_open,
	.lseek = __lseek,
	.write = __write,
	.resize = NULL,
};

#ifndef CORELIB
struct r_lib_struct_t radare_plugin = {
	.type = R_LIB_TYPE_IO,
	.data = &r_io_plugin_sparse,
	.version = R2_VERSION
};
#endif
