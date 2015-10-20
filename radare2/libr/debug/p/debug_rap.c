/* radare - LGPL - Copyright 2011-2012 - pancake */

#include <r_asm.h>
#include <r_cons.h>
#include <r_debug.h>

static int r_debug_rap_step(RDebug *dbg) {
	r_io_system (dbg->iob.io, "ds");
	return R_TRUE;
}

static int r_debug_rap_reg_read(RDebug *dbg, int type, ut8 *buf, int size) {
	r_io_system (dbg->iob.io, "dr");
	return 0;
}

static int r_debug_rap_reg_write(RDebug *dbg, int type, const ut8 *buf, int size) {
	return R_FALSE; // XXX Error check
}

static int r_debug_rap_continue(RDebug *dbg, int pid, int tid, int sig) {
	r_io_system (dbg->iob.io, "dc");
	return R_TRUE;
}

static int r_debug_rap_wait(RDebug *dbg, int pid) {
	/* do nothing */
	return R_TRUE;
}

static int r_debug_rap_attach(RDebug *dbg, int pid) {
// XXX TODO PID must be a socket here !!1
	RIODesc *d = dbg->iob.io->desc;
	if (d && d->plugin && d->plugin->name) {
		if (!strcmp ("rap", d->plugin->name)) {
			eprintf ("SUCCESS: rap attach with inferior rap rio worked\n");
		} else {
			eprintf ("ERROR: Underlaying IO descriptor is not a GDB one..\n");
		}
	}
	return R_TRUE;
}

static int r_debug_rap_detach(int pid) {
// XXX TODO PID must be a socket here !!1
//	close (pid);
	//XXX Maybe we should continue here?
	return R_TRUE;
}

static char *r_debug_rap_reg_profile(RDebug *dbg) {
	char *out, *tf = r_file_temp ("/tmp/rap.XXXXXX");
	int fd = r_cons_pipe_open (tf, 1, 0);
	r_io_system (dbg->iob.io, "drp");
	r_cons_pipe_close (fd);
	out = r_file_slurp (tf, NULL);
	r_file_rm (tf);
	free (tf);
	return out;
}

static int r_debug_rap_breakpoint (RBreakpointItem *bp, int set, void *user){
	//r_io_system (dbg->iob.io, "db");
	return R_FALSE;
}

RDebugPlugin r_debug_plugin_rap = {
	.name = "rap",
	.license = "LGPL3",
	/* TODO: Add support for more architectures here */
	.arch = 0xff,
	.bits = R_SYS_BITS_32,
	.init = NULL,
	.step = r_debug_rap_step,
	.cont = r_debug_rap_continue,
	.attach = &r_debug_rap_attach,
	.detach = &r_debug_rap_detach,
	.wait = &r_debug_rap_wait,
	.pids = NULL,
	.tids = NULL,
	.threads = NULL,
	.kill = NULL,
	.frames = NULL,
	.map_get = NULL,
	.breakpoint = &r_debug_rap_breakpoint,
	.reg_read = &r_debug_rap_reg_read,
	.reg_write = &r_debug_rap_reg_write,
	.reg_profile = (void *)r_debug_rap_reg_profile,
	//.bp_write = &r_debug_rap_bp_write,
	//.bp_read = &r_debug_rap_bp_read,
};

#ifndef CORELIB
struct r_lib_struct_t radare_plugin = {
	.type = R_LIB_TYPE_DBG,
	.data = &r_debug_plugin_rap,
	.version = R2_VERSION
};
#endif
