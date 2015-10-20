/* radare - LGPL - Copyright 2009-2015 - pancake */

#include <r_debug.h>
#include <r_asm.h>
#include <r_reg.h>
#include <r_lib.h>
#include <r_anal.h>
#include "linux_debug.h"

const char *linux_reg_profile (RDebug *dbg) {
#if __arm__
#include "reg/linux-arm.h"
#elif __arm64__ || __aarch64__
#include "reg/linux-arm64.h"
#elif __MIPS__ || __mips__
#include "reg/linux-mips.h"
#elif (__i386__ || __x86_64__)
	if (dbg->bits & R_SYS_BITS_32) {
#if __x86_64__
#include "reg/linux-x64-32.h"
#else
#include "reg/linux-x86.h"
#endif
	} else {
#include "reg/linux-x64.h"
	}
#else
#error "Unsupported Linux CPU"
#endif
}

int linux_handle_signals (RDebug *dbg) {
	siginfo_t siginfo = {0};
	int ret = ptrace (PTRACE_GETSIGINFO, dbg->pid, 0, &siginfo);
	if (ret != -1 && siginfo.si_signo > 0) {
		//siginfo_t newsiginfo = {0};
		//ptrace (PTRACE_SETSIGINFO, dbg->pid, 0, &siginfo);
		dbg->reason.type = R_DEBUG_REASON_SIGNAL;
		dbg->reason.signum = siginfo.si_signo;
		//dbg->stopaddr = siginfo.si_addr;
		//dbg->errno = siginfo.si_errno;
		// siginfo.si_code -> HWBKPT, USER, KERNEL or WHAT
#warning DO MORE RDEBUGREASON HERE
		switch (dbg->reason.signum) {
		case SIGABRT: // 6 / SIGIOT // SIGABRT
			dbg->reason.type = R_DEBUG_REASON_ABORT;
			break;
		case SIGSEGV:
			dbg->reason.type = R_DEBUG_REASON_SEGFAULT;
			eprintf ("[+] SIGNAL %d errno=%d addr=%p code=%d ret=%d\n",
				siginfo.si_signo, siginfo.si_errno,
				siginfo.si_addr, siginfo.si_code, ret);
			break;
		default: break;
		}
		return R_TRUE;
	}
	return R_FALSE;

}

int linux_step (RDebug *dbg) {
	int ret = R_FALSE;
	ut64 addr = 0; /* should be eip */
	//ut32 data = 0;
	//printf("NATIVE STEP over PID=%d\n", pid);
	addr = r_debug_reg_get (dbg, "pc");
	ret = ptrace (PTRACE_SINGLESTEP, dbg->pid,
			(void*)(size_t)addr, 0);
	linux_handle_signals (dbg);
	if (ret == -1) {
		perror ("native-singlestep");
		ret = R_FALSE;
	} else {
		ret = R_TRUE;
	}
	return ret;

}

int linux_attach (RDebug *dbg, int pid) {
	int ret = -1;
	int traceflags = 0;
	if (dbg->trace_forks) {
		traceflags |= PTRACE_O_TRACEFORK;
		traceflags |= PTRACE_O_TRACEVFORK;
		traceflags |= PTRACE_O_TRACEVFORKDONE;
	}
	if (dbg->trace_clone) {
		// threads
		traceflags |= PTRACE_O_TRACECLONE;
	}
	//traceflags |= PTRACE_O_TRACESYSGOOD; mark 0x80| on signal event, x86-only
	if (dbg->trace_execs) {
		traceflags |= PTRACE_O_TRACEEXEC;
	}
	traceflags |= PTRACE_O_TRACEEXIT;
	if (ptrace (PTRACE_SETOPTIONS, pid, 0, traceflags) == -1) {
		/* ignore ptrace-options errors */
	}
	ret = ptrace (PTRACE_ATTACH, pid, 0, 0);
	if (ret != -1) perror ("ptrace (PT_ATTACH)");
	return pid;

}

RDebugInfo *linux_info (RDebug *dbg, const char *arg) {
	char procpid_cmdline[1024];
	RDebugInfo *rdi = R_NEW0 (RDebugInfo);
	if (!rdi) return NULL;
	rdi->status = R_DBG_PROC_SLEEP; // TODO: Fix this
	rdi->pid = dbg->pid;
	rdi->tid = dbg->tid;
	rdi->uid = -1;// TODO
	rdi->gid = -1;// TODO
	rdi->cwd = NULL;// TODO : use readlink
	rdi->exe = NULL;// TODO : use readlink!
	snprintf (procpid_cmdline, sizeof(procpid_cmdline), 
		"/proc/%d/cmdline", rdi->pid);
	rdi->cmdline = r_file_slurp (procpid_cmdline, NULL);
	return rdi;

}


RList *linux_thread_list (int pid, RList *list) {
	int i, fd = -1, thid = 0;
	char *ptr, cmdline[1024];

	if (!pid) {
		r_list_free (list);
		return NULL;
	}
	r_list_append (list, r_debug_pid_new ("(current)", pid, 's', 0));
	/* list parents */

	/* LOL! linux hides threads from /proc, but they are accessible!! HAHAHA */
	//while ((de = readdir (dh))) {
	snprintf (cmdline, sizeof(cmdline), "/proc/%d/task", pid);
	if (r_file_exists (cmdline)) {
		struct dirent *de;
		DIR *dh = opendir (cmdline);
		while ((de = readdir (dh))) {
			int tid = atoi (de->d_name);
			// TODO: get status, pc, etc..
			r_list_append (list, r_debug_pid_new (cmdline, tid, 's', 0));
		}
		closedir (dh);
	} else {
		/* LOL! linux hides threads from /proc, but they are accessible!! HAHAHA */
		//while ((de = readdir (dh))) {
#undef MAXPID
#define MAXPID 99999
		for (i = pid; i < MAXPID; i++) { // XXX
			snprintf (cmdline, sizeof(cmdline), "/proc/%d/status", i);
			if (fd != -1)
				close (fd);
			fd = open (cmdline, O_RDONLY);
			if (fd == -1) continue;
			if (read (fd, cmdline, 1024)<2) {
				// read error
				close (fd);
				break;
			}
			cmdline[sizeof(cmdline) - 1] = '\0';
			ptr = strstr (cmdline, "Tgid:");
			if (ptr) {
				int tgid = atoi (ptr + 5);
				if (tgid != pid) {
					close (fd);
					continue;
				}
				if (read (fd, cmdline, sizeof(cmdline) - 1) <2) {
					break;
				}
				snprintf (cmdline, sizeof(cmdline), "thread_%d", thid++);
				cmdline[sizeof (cmdline) - 1] = '\0';
				r_list_append (list, r_debug_pid_new (cmdline, i, 's', 0));
			}
		}
		if (fd != -1) {
			close (fd);
			fd = -1;
		}
	}
	return list;
}

#define PRINT_FPU(fpregs) \
	eprintf ("cwd = 0x%04x  ; control   ", (fpregs).cwd);\
	eprintf ("swd = 0x%04x  ; status\n", (fpregs).swd);\
	eprintf ("ftw = 0x%04x              ", (fpregs).ftw);\
	eprintf ("fop = 0x%04x\n", fpregs.fop);\
	eprintf ("rip = 0x%016"PFMT64x"  ", (ut64)(fpregs).rip);\
	eprintf ("rdp = 0x%016"PFMT64x"\n", (ut64)(fpregs).rdp);\
	eprintf ("mxcsr = 0x%08x        ", (fpregs).mxcsr);\
	eprintf ("mxcr_mask = 0x%08x\n", (fpregs).mxcr_mask)\

#define PRINT_FPU_NOXMM(fpregs) \
	eprintf ("cwd = 0x%04lx  ; control   ", (fpregs).cwd);\
	eprintf ("swd = 0x%04lx  ; status\n", (fpregs).swd);\
	eprintf ("twd = 0x%04lx              ", (fpregs).twd);\
	eprintf ("fip = 0x%04lx          \n", (fpregs).fip);\
	eprintf ("fcs = 0x%04lx              ", (fpregs).fcs);\
	eprintf ("foo = 0x%04lx          \n", (fpregs).foo);\
	eprintf ("fos = 0x%04lx              ", (fpregs).fos)


static void print_fpu (void *f, int r){
#if __x86_64__ || __i386__
	int i;
	struct user_fpregs_struct fpregs = *(struct user_fpregs_struct*)f;
#if __x86_64__
#if !__ANDROID__
	eprintf ("---- x86-64 ----\n");
	PRINT_FPU (fpregs);
	eprintf ("size = 0x%08x\n", (ut32)sizeof (fpregs));
	for (i = 0; i < 16; i++) {
		ut32 *a = (ut32*)&fpregs.xmm_space;
		a = a + (i * 4);
		eprintf ("xmm%d = %08x %08x %08x %08x   ", i, (int)a[0], (int)a[1],
			(int)a[2], (int)a[3] );
		if (i < 8) {
			ut64 *b = (ut64*)&fpregs.st_space[i * 4];
			ut32 *c = (ut32*)&fpregs.st_space;
			float *f = (float *)&fpregs.st_space;
			c = c + (i * 4);
			f = f + (i * 4);
			eprintf ("st%d =%0.3lg (0x%016"PFMT64x") | %0.3f (%08x)  |\
				%0.3f (%08x) \n", i,
				(double)*((double*)&fpregs.st_space[i*4]), *b,
				(float)f[0], c[0], (float)f[1], c[1]);
		} else {
			eprintf ("\n");
		}
	}
#else
	PRINT_FPU (fpregs);
	for(i = 0;i < 8; i++) {
		ut64 *b = (ut64 *)&fpregs.st_space[i*4];
		ut32 *c = (ut32*)&fpregs.st_space;
		float *f = (float *)&fpregs.st_space;
		c = c + (i * 4);
		f = f + (i * 4);
		eprintf ("st%d =%0.3lg (0x%016"PFMT64x") | %0.3f (%08x)  | \
			%0.3f (%08x) \n", i,
			(double)*((double*)&fpregs.st_space[i*4]), *b, (float) f[0],
			c[0], (float) f[1], c[1]);
	}
#endif	// !__ANDROID__
#elif __i386__
	if (!r) {
		#if !__ANDROID__
			struct user_fpxregs_struct fpxregs = 
											*(struct user_fpxregs_struct*)f;
			eprintf ("---- x86-32 ----\n");
			eprintf ("cwd = 0x%04x  ; control   ", fpxregs.cwd);
			eprintf ("swd = 0x%04x  ; status\n", fpxregs.swd);
			eprintf ("twd = 0x%04x ", fpxregs.twd);
			eprintf ("fop = 0x%04x\n", fpxregs.fop);
			eprintf ("fip = 0x%08x\n", fpxregs.fip);
			eprintf ("fcs = 0x%08x\n", fpxregs.fcs);
			eprintf ("foo = 0x%08x\n", fpxregs.foo);
			eprintf ("fos = 0x%08x\n", fpxregs.fos);
			eprintf ("mxcsr = 0x%08x\n", fpxregs.mxcsr);
			for(i = 0; i < 8; i++) {
				ut32 *a = (ut32*)(&fpxregs.xmm_space);
				ut64 *b = (ut64 *)(&fpxregs.st_space[i * 4]);
				ut32 *c = (ut32*)&fpxregs.st_space;
				float *f = (float *)&fpxregs.st_space;
				a = a + (i * 4);
				c = c + (i * 4);
				f = f + (i * 4);
				eprintf ("xmm%d = %08x %08x %08x %08x   ", i, (int)a[0],
					(int)a[1], (int)a[2], (int)a[3] );
				eprintf ("st%d = %0.3lg (0x%016"PFMT64x") | %0.3f (0x%08x) |\
					%0.3f (0x%08x)\n", i,
					(double)*((double*)(&fpxregs.st_space[i*4])), b[0],
					f[0], c[0], f[1], c[1]);
			}
#endif // !__ANDROID__
			return;
	}
	eprintf ("---- x86-32-noxmm ----\n");
	PRINT_FPU_NOXMM (fpregs);
	for(i = 0; i < 8; i++) {
		ut64 *b = (ut64 *)(&fpregs.st_space[i*4]);
		double *d = (double*)b;
		ut32 *c = (ut32*)&fpregs.st_space;
		float *f = (float *)&fpregs.st_space;
		c = c + (i * 4);
		f = f + (i * 4);
		eprintf ("st%d = %0.3lg (0x%016"PFMT64x") | %0.3f (0x%08x)  | \
			%0.3f (0x%08x)\n", i, d[0], b[0], f[0], c[0], f[1], c[1]);
	}
#endif
#else 
#warning not implemented for this platform
#endif
}



int linux_reg_read (RDebug *dbg, int type, ut8 *buf, int size) {
	int showfpu = R_FALSE;
	int pid = dbg->pid;
	int ret;
	if (type < -1) {
		showfpu = R_TRUE;
		type = -type;
	}
	switch (type) {
	case R_REG_TYPE_DRX:
#if __i386__ || __x86_64__
#if !__ANDROID__
	{
		int i;
		for (i = 0; i < 8; i++) { //DR0-DR7
			if (i == 4 || i == 5) continue;
			long ret = ptrace (PTRACE_PEEKUSER, pid, 
					r_offsetof (struct user, u_debugreg[i]), 0);
			memcpy (buf + (i * sizeof(ret)), &ret, sizeof(ret));
		}
		return sizeof(R_DEBUG_REG_T);
	}
#else
	#warning Android X86 does not support DRX
#endif
#endif
		return R_TRUE;
		break;
	case R_REG_TYPE_FPU:
	case R_REG_TYPE_MMX:
	case R_REG_TYPE_XMM:
#if __x86_64__ || __i386__
		{
		int ret1 = 0;
		struct user_fpregs_struct fpregs;
		if (type == R_REG_TYPE_FPU) {
#if __x86_64__
#if !__ANDROID__
			ret1 = ptrace (PTRACE_GETFPREGS, pid, NULL, &fpregs);
			if (showfpu) print_fpu ((void *)&fpregs, 0);
			if (ret1 != 0) return R_FALSE;
			if (sizeof(fpregs) < size) size = sizeof(fpregs);
			memcpy (buf, &fpregs, size);
			return sizeof(fpregs);
#else
			ret1 = ptrace (PTRACE_GETFPREGS, pid, NULL, &fpregs);
			if (showfpu) print_fpu ((void *)&fpregs, 0);
			if (ret1 != 0) return R_FALSE;
			if (sizeof(fpregs) < size) size = sizeof(fpregs);
			memcpy (buf, &fpregs, size);
			return sizeof(fpregs)
#endif // !__ANDROID__
#elif __i386__
#if !__ANDROID__
			struct user_fpxregs_struct fpxregs;
			ret1 = ptrace (PTRACE_GETFPXREGS, pid, NULL, &fpxregs);
			if (ret1 == 0) {
				if (showfpu) print_fpu ((void *)&fpxregs, ret1);
				if (sizeof(fpxregs) < size) size = sizeof(fpxregs);
				memcpy (buf, &fpxregs, size);
				return sizeof(fpxregs);
			} else {
				ret1 = ptrace (PTRACE_GETFPREGS, pid, NULL, &fpregs);
				if (showfpu) print_fpu ((void *)&fpregs, ret1);
				if (ret1 != 0) return R_FALSE;
				if (sizeof(fpregs) < size) size = sizeof(fpregs);
				memcpy (buf, &fpregs, size);
				return sizeof(fpregs);
			}
#else
			ret1 = ptrace (PTRACE_GETFPREGS, pid, NULL, &fpregs);
			if (showfpu) print_fpu ((void *)&fpregs, 1);
			if (ret1 != 0) return R_FALSE;
			if (sizeof(fpregs) < size) size = sizeof(fpregs);
			memcpy (buf, &fpregs, size);
			return sizeof(fpregs);
#endif // !__ANDROID__
#endif // __i386__
		}
		}
#else
	#warning not implemented for this platform
#endif
		break;
	case R_REG_TYPE_SEG:
	case R_REG_TYPE_FLG:
	case R_REG_TYPE_GPR:
		{
			R_DEBUG_REG_T regs;
			memset (&regs, 0, sizeof(regs));
			memset (buf, 0, size);
#if __powerpc__
			ret = ptrace (PTRACE_GETREGS, pid, &regs, NULL);
#elif !__powerpc__
			/* linux -{arm/x86/x86_64} */
			ret = ptrace (PTRACE_GETREGS, pid, NULL, &regs);
#endif
			/*
			 * if perror here says 'no such process' and the 
			 * process exists still.. is because there's a missing call 
			 * to 'wait'. and the process is not yet available to accept 
			 * more ptrace queries.
			 */
			if (ret != 0) return R_FALSE;
			if (sizeof(regs) < size) size = sizeof(regs);
			memcpy (buf, &regs, size);
			return sizeof (regs);
		}
		break;
	}
	return R_TRUE;

}

int linux_reg_write (RDebug *dbg, int type, const ut8 *buf, int size) {
	if (type == R_REG_TYPE_DRX) {
// XXX: this android check is only for arm
#if !__ANDROID__
		{
		int i;
		long *val = (long*)buf;
		for (i = 0; i < 8; i++) { // DR0-DR7
			if (i == 4 || i == 5) continue;
			long ret = ptrace (PTRACE_POKEUSER, dbg->pid,
					r_offsetof (struct user, u_debugreg[i]),
					val[i]);
			if (ret != 0) {
				eprintf ("ptrace error for dr %d\n", i);
				perror ("ptrace");
				//return R_FALSE;
			}
		}
		}
		return sizeof(R_DEBUG_REG_T);
#else
		return R_FALSE;
#endif
	}
	if (type == R_REG_TYPE_GPR) {
		int ret = ptrace (PTRACE_SETREGS, dbg->pid, 0, (void*)buf);
		if (sizeof(R_DEBUG_REG_T) < size) size = sizeof(R_DEBUG_REG_T);
		return (ret != 0) ? R_FALSE : R_TRUE;
	}
	return R_FALSE;
}


RList *linux_desc_list (int pid) {
	RList *ret = NULL;
	char path[512], file[512], buf[512];
	struct dirent *de;
	RDebugDesc *desc;
	int type, perm;
	int len, len2;
	struct stat st;
	DIR *dd = NULL;

	snprintf (path, sizeof (path), "/proc/%i/fd/", pid);
	if (!(dd = opendir (path))) {
		eprintf ("Cannot open /proc\n");
		return NULL;
	}
	ret = r_list_new ();
	if (!ret) {
		closedir (dd);
		return NULL;
	}
	ret->free = (RListFree)r_debug_desc_free;
	while ((de = (struct dirent *)readdir(dd))) {
		if (de->d_name[0] == '.') continue;
		len = strlen (path);
		len2 = strlen (de->d_name);
		if (len + len2 + 1 >= sizeof(file)) {
			r_list_free (ret);
			closedir (dd);
			eprintf ("Filename is too long");
			return NULL;
		}
		memcpy (file, path, len);
		memcpy (file + len, de->d_name, len2 + 1);
		memset (buf, 0, sizeof(buf));
		readlink (file, buf, sizeof (buf) - 1);
		buf[sizeof (buf)-1] = 0;
		type = perm = 0;
		if (stat (file, &st) != -1) {
			type  = st.st_mode & S_IFIFO  ? 'P':
#ifdef S_IFSOCK
				st.st_mode & S_IFSOCK ? 'S':
#endif
				st.st_mode & S_IFCHR  ? 'C':'-';
		}
		if (lstat(path, &st) != -1) {
			if (st.st_mode & S_IRUSR) perm |= R_IO_READ;
			if (st.st_mode & S_IWUSR) perm |= R_IO_WRITE;
		}
		//TODO: Offset
		desc = r_debug_desc_new (atoi (de->d_name), buf, perm, type, 0);
		if (!desc) break;
		r_list_append (ret, desc);
	}
	closedir(dd);
	return ret;
}
