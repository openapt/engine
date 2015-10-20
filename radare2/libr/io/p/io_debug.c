/* radare - LGPL - Copyright 2007-2015 - pancake */

#include <r_io.h>
#include <r_lib.h>
#include <r_util.h>
#include <r_debug.h> /* only used for BSD PTRACE redefinitions */

#define USE_RARUN 0

#if __linux__ ||  __APPLE__ || __WINDOWS__ || \
	__NetBSD__ || __KFBSD__ || __OpenBSD__
#define DEBUGGER_SUPPORTED 1
#else
#define DEBUGGER_SUPPORTED 0
#endif

#if DEBUGGER && DEBUGGER_SUPPORTED

static void my_io_redirect (RIO *io, const char *ref, const char *file) {
	free (io->referer);
	io->referer = ref? strdup (ref): NULL;
	free (io->redirect);
	io->redirect = file? strdup (file): NULL;
}

#define MAGIC_EXIT 123

#include <signal.h>
#if __UNIX__
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif

#if __APPLE__
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <mach/exception_types.h>
#include <mach/mach_init.h>
#include <mach/mach_port.h>
#include <mach/mach_traps.h>
#include <mach/task.h>
#include <mach/task_info.h>
#include <mach/thread_act.h>
#include <mach/thread_info.h>
#include <mach/vm_map.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#endif

#if __APPLE__ || __BSD__
static void inferior_abort_handler(int pid) {
        eprintf ("Inferior received signal SIGABRT. Executing BKPT.\n");
}
#endif

/*
 * Creates a new process and returns the result:
 * -1 : error
 *  0 : ok
 */
#if __WINDOWS__
#include <windows.h>
#include <tlhelp32.h>
#include <winbase.h>
#include <psapi.h>

static int setup_tokens() {
        HANDLE tok;
        TOKEN_PRIVILEGES tp;
        DWORD err;

        tok = NULL;
        err = -1;

        if (!OpenProcessToken (GetCurrentProcess (), TOKEN_ADJUST_PRIVILEGES, &tok))
		goto err_enable;

        tp.PrivilegeCount = 1;
        if (!LookupPrivilegeValue (NULL,  SE_DEBUG_NAME, &tp.Privileges[0].Luid))
		goto err_enable;

        //tp.Privileges[0].Attributes = enable ? SE_PRIVILEGE_ENABLED : 0;
        tp.Privileges[0].Attributes = 0; //SE_PRIVILEGE_ENABLED;
        if (!AdjustTokenPrivileges (tok, 0, &tp, sizeof (tp), NULL, NULL))
		goto err_enable;
        err = 0;
err_enable:
        if (tok != NULL) CloseHandle (tok);
        if (err) r_sys_perror ("setup_tokens");
        return err;
}

static int fork_and_ptraceme(RIO *io, int bits, const char *cmd) {
	PROCESS_INFORMATION pi;
        STARTUPINFO si = { sizeof (si) };
        DEBUG_EVENT de;
	int pid, tid;
	HANDLE th = INVALID_HANDLE_VALUE;
	if (!*cmd) return -1;
	setup_tokens ();
	char *_cmd = io->args ? r_str_concatf (strdup (cmd), " %s", io->args) :
				strdup (cmd);
	char **argv = r_str_argv (_cmd, NULL);
	// We need to build a command line with quoted argument and escaped quotes
	int cmd_len = 0;
	int i = 0;
	while (argv[i]) {
		char *current = argv[i];
		int quote_count = 0;
		while ((current = strchr (current, '"')))
			quote_count ++;
		cmd_len += strlen (argv[i]);
		cmd_len += quote_count; // The quotes will add one backslash each
		cmd_len += 2; // Add two enclosing quotes;
		i++;
	}
	cmd_len += i-1; // Add argc-1 spaces

	char *cmdline = malloc ((cmd_len + 1) * sizeof (char));
	int cmd_i = 0; // Next character to write in cmdline
	i = 0;
	while (argv[i]) {
		if (i != 0)
			cmdline[cmd_i++] = ' ';

		cmdline[cmd_i++] = '"';

		int arg_i = 0; // Index of current character in orginal argument
		while (argv[i][arg_i]) {
			char c = argv[i][arg_i];
			if (c == '"') {
				cmdline[cmd_i++] = '\\';
			}
			cmdline[cmd_i++] = c;
			arg_i++;
		}

		cmdline[cmd_i++] = '"';
		i++;
	}
	cmdline[cmd_i] = '\0';

        if (!CreateProcess (argv[0], cmdline, NULL, NULL, FALSE,
			CREATE_NEW_CONSOLE | DEBUG_ONLY_THIS_PROCESS,
			NULL, NULL, &si, &pi)) {
			r_sys_perror ("CreateProcess");
		return -1;
        }
	free (cmdline);
	r_str_argv_free (argv);
        /* get process id and thread id */
        pid = pi.dwProcessId;
        tid = pi.dwThreadId;

#if 0
        /* load thread list */
	{
		HANDLE h;
		THREADENTRY32 te32;
		HANDLE WINAPI (*win32_openthread)(DWORD, BOOL, DWORD) = NULL;
		win32_openthread = (HANDLE WINAPI (*)(DWORD, BOOL, DWORD))
			GetProcAddress (GetModuleHandle ("kernel32"), "OpenThread");

		th = CreateToolhelp32Snapshot (TH32CS_SNAPTHREAD, pid);
		if (th == INVALID_HANDLE_VALUE || !Thread32First(th, &te32))
			r_sys_perror ("CreateToolhelp32Snapshot");

		do {
			if (te32.th32OwnerProcessID == pid) {
				h = win32_openthread (THREAD_ALL_ACCESS, 0, te32.th32ThreadID);
				if (h == NULL) r_sys_perror ("OpenThread");
				else eprintf ("HANDLE=%p\n", h);
			}
		} while (Thread32Next (th, &te32));
	}
#endif

#if 0
	// Access denied here :?
	if (ContinueDebugEvent (pid, tid, DBG_CONTINUE) == 0) {
		r_sys_perror ("ContinueDebugEvent");
		goto err_fork;
	}
#endif

        /* catch create process event */
        if (!WaitForDebugEvent (&de, 10000)) goto err_fork;

        /* check if is a create process debug event */
        if (de.dwDebugEventCode != CREATE_PROCESS_DEBUG_EVENT) {
		eprintf ("exception code 0x%04x\n", (ut32)de.dwDebugEventCode);
		goto err_fork;
        }

	if (th != INVALID_HANDLE_VALUE) CloseHandle (th);

	eprintf ("Spawned new process with pid %d, tid = %d\n", pid, tid);
        return pid;

err_fork:
	eprintf ("ERRFORK\n");
        TerminateProcess (pi.hProcess, 1);
	if (th != INVALID_HANDLE_VALUE) CloseHandle (th);
        return -1;
}
#else // windows

static void trace_me () {
#if __APPLE__
	signal (SIGTRAP, SIG_IGN); //NEED BY STEP
#endif
#if __APPLE__ || __BSD__
/* we can probably remove this #if..as long as PT_TRACE_ME is redefined for OSX in r_debug.h */
	signal (SIGABRT, inferior_abort_handler);
	if (ptrace (PT_TRACE_ME, 0, 0, 0) != 0) {
#else
	if (ptrace (PTRACE_TRACEME, 0, NULL, NULL) != 0) {
#endif
		r_sys_perror ("ptrace-traceme");
		exit (MAGIC_EXIT);
	}
}

// __UNIX__ (not windows)
static int fork_and_ptraceme(RIO *io, int bits, const char *cmd) {
	bool runprofile = io->runprofile && *(io->runprofile);
	char **argv;

#if __APPLE__
	if (!runprofile) {
#define _POSIX_SPAWN_DISABLE_ASLR 0x0100
		posix_spawn_file_actions_t fileActions;
		ut32 ps_flags = POSIX_SPAWN_SETSIGDEF |
				POSIX_SPAWN_SETSIGMASK;
		posix_spawnattr_t attr = {0};
		size_t copied = 1;
		cpu_type_t cpu;
		pid_t p = -1;
		int ret, useASLR = io->aslr;
		char *_cmd = io->args ?
				r_str_concatf (strdup (cmd), " %s", io->args) :
				strdup (cmd);
		argv = r_str_argv (_cmd, NULL);
		if (!argv) {
			free (_cmd);
			return -1;
		}
		if (!*argv) {
			r_str_argv_free (argv);
			free (_cmd);
			eprintf ("Invalid execvp\n");
			return -1;
		}
		posix_spawnattr_init (&attr);
		if (useASLR != -1) {
			if (!useASLR) ps_flags |= _POSIX_SPAWN_DISABLE_ASLR;
		}

		posix_spawn_file_actions_init (&fileActions);
		posix_spawn_file_actions_addinherit_np (&fileActions, STDIN_FILENO);
		posix_spawn_file_actions_addinherit_np (&fileActions, STDOUT_FILENO);
		posix_spawn_file_actions_addinherit_np (&fileActions, STDERR_FILENO);
		ps_flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
		ps_flags |= POSIX_SPAWN_START_SUSPENDED;

		(void)posix_spawnattr_setflags (&attr, ps_flags);
#if __i386__ || __x86_64__
		cpu = CPU_TYPE_I386;
		if (bits == 64) cpu |= CPU_ARCH_ABI64;
#else
		cpu = CPU_TYPE_ANY;
#endif
		posix_spawnattr_setbinpref_np (&attr, 1, &cpu, &copied);
		ret = posix_spawnp (&p, argv[0], &fileActions, &attr, argv, NULL);
		switch (ret) {
		case 0:
			eprintf ("Success\n");
			break;
		case 22:
			eprintf ("posix_spawnp: Invalid argument\n");
			break;
		case 86:
			eprintf ("Unsupported architecture\n");
			break;
		default:
			eprintf ("posix_spawnp: unknown error %d\n", ret);
			perror ("posix_spawnp");
			break;
		}
		posix_spawn_file_actions_destroy (&fileActions);
		r_str_argv_free (argv);
		free (_cmd);
		return p;
	}
#endif

	int ret, status, pid = r_sys_fork ();
	switch (pid) {
	case -1:
		perror ("fork_and_ptraceme");
		break;
	case 0:
		trace_me ();
		if (runprofile) {
			char *expr = NULL;
			int i;
			RRunProfile *rp = r_run_new (NULL);
			argv = r_str_argv (cmd, NULL);
			for (i = 0; argv[i]; i++) {
				rp->_args[i] = argv[i];
			}
			rp->_args[i] = NULL;
			rp->_program = argv[0];
			rp->_dodebug = true;
			if (io->runprofile && *io->runprofile) {
				if (!r_run_parsefile (rp, io->runprofile)) {
					eprintf ("Can't find profile '%s'\n",
						io->runprofile);
					exit (MAGIC_EXIT);
				}
			}
			if (bits == 64) {
				r_run_parseline (rp, expr=strdup ("bits=64"));
			} else if (bits == 32) {
				r_run_parseline (rp, expr=strdup ("bits=32"));
			}
			free (expr);
			r_run_start (rp);
			r_run_free (rp);
			r_str_argv_free (argv);
			exit (1);
		} else {
			char *_cmd = io->args ?
				r_str_concatf (strdup (cmd), " %s", io->args) :
				strdup (cmd);
			argv = r_str_argv (_cmd, NULL);
			if (!argv) {
				free (_cmd);
				return -1;
			}
			if (argv && *argv) {
				int i;
				for (i = 3; i < 1024; i++) {
					(void)close (i);
				}
				execvp (argv[0], argv);
			} else {
				eprintf ("Invalid execvp\n");
			}
			r_str_argv_free (argv);
			free (_cmd);
		}
		perror ("fork_and_attach: execv");
		//printf(stderr, "[%d] %s execv failed.\n", getpid(), ps.filename);
		exit (MAGIC_EXIT); /* error */
		return 0; // invalid pid // if exit is overriden.. :)
	default:
		/* XXX: clean this dirty code */
		do {
			ret = wait (&status);
			if (ret == -1) return -1;
			if (ret != pid) eprintf ("Wait event received by "
						"different pid %d\n", ret);
		} while (ret != pid);
		if (WIFSTOPPED (status))
			eprintf ("Process with PID %d started...\n", (int)pid);
		if (WEXITSTATUS (status) == MAGIC_EXIT)
			pid = -1;
		// XXX kill (pid, SIGSTOP);
		break;
	}
	return pid;
}
#endif

static int __plugin_open(RIO *io, const char *file, ut8 many) {
	return (!strncmp (file, "dbg://", 6) && file[6]);
}

static RIODesc *__open(RIO *io, const char *file, int rw, int mode) {
	char uri[128];
	if (__plugin_open (io, file,  0)) {
		const char *pidfile = file + 6;
		char *endptr;
		int pid = (int)strtol (pidfile, &endptr, 10);
		if (endptr == pidfile || pid < 0) pid = -1;

		if (pid == -1) {
			pid = fork_and_ptraceme (io, io->bits, file+6);
			if (pid == -1)
				return NULL;
#if __WINDOWS__
			sprintf (uri, "w32dbg://%d", pid);
#elif __APPLE__
			sprintf (uri, "mach://%d", pid);
#else
			// TODO: use io_procpid here? faster or what?
			sprintf (uri, "ptrace://%d", pid);
#endif
			my_io_redirect (io, file, uri);
		} else {
			sprintf (uri, "attach://%d", pid);
			my_io_redirect (io, file, uri);
		}
		return NULL;
	}
	my_io_redirect (io, file, NULL);
	return NULL;
}

RIOPlugin r_io_plugin_debug = {
	.name = "debug",
        .desc = "Debug a program or pid. dbg:///bin/ls, dbg://1388",
	.license = "LGPL3",
        .open = __open,
        .plugin_open = __plugin_open,
	.lseek = NULL,
	.system = NULL,
	.isdbg = true,
};
#else
struct r_io_plugin_t r_io_plugin_debug = {
	.name = "debug",
        .desc = "Debug a program or pid. (NOT SUPPORTED FOR THIS PLATFORM)",
};
#endif

#ifndef CORELIB
struct r_lib_struct_t radare_plugin = {
	.type = R_LIB_TYPE_IO,
	.data = &r_io_plugin_debug,
	.version = R2_VERSION
};
#endif
