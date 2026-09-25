// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2025 OpenZFS on Windows contributors.
 */

/*
 * UAC elevation helpers for Windows CLI tools (zfs, zpool).
 *
 * When a non-elevated process needs to perform an admin operation it
 * calls windows_relaunch_elevated(), which:
 *   1. Creates two named pipes:
 *        <base>-out  INBOUND  — child stdout/stderr → parent console
 *        <base>-in   OUTBOUND — parent console stdin → child stdin
 *   2. Re-launches the current process via ShellExecuteEx "runas" (UAC),
 *      appending "--zfs-elev-pipe <base>" to the command line.
 *   3. Relays I/O between the parent console and the elevated child using
 *      WaitForMultipleObjects on the stdout pipe, the parent stdin handle,
 *      and the child process handle.
 *   4. Exits with the child's return code.
 *
 * NOTE: environment variables set via SetEnvironmentVariableA() are NOT
 * inherited by processes launched through ShellExecuteEx "runas" — UAC
 * rebuilds the environment from the user profile.  The pipe base name is
 * therefore passed as a hidden trailing command-line argument instead.
 *
 * The elevated child calls windows_elevate_child_init(&argc, argv) at
 * the start of main().  It derives <base>-out and <base>-in from the
 * base name, connects to both pipes, and redirects all three standard
 * streams through them so I/O is fully relayed through the parent's
 * console.
 *
 * The library provides these functions but never calls them itself.
 */

#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
#include <unistd.h>

#define	ZFS_ELEVATE_ARG		"--zfs-elev-pipe"
#define	ZFS_ELEVATE_OUT_SFX	"-out"
#define	ZFS_ELEVATE_IN_SFX	"-in"

/*
 * Argument block passed to the stdin relay thread.
 */
typedef struct {
	HANDLE hstdin;
	HANDLE hpipe_in;
} stdin_relay_args_t;

/*
 * Background thread: forwards data from the parent's console stdin to the
 * child's stdin pipe.  Exits when WriteFile fails (pipe closed) or
 * ReadFile returns 0 bytes.  The args block is freed by this thread.
 */
static DWORD WINAPI
stdin_relay_thread(LPVOID param)
{
	stdin_relay_args_t *args = (stdin_relay_args_t *)param;
	char buf[4096];
	DWORD got, written;

	while (ReadFile(args->hstdin, buf, sizeof (buf), &got, NULL) &&
	    got > 0) {
		if (!WriteFile(args->hpipe_in, buf, got, &written, NULL))
			break;
	}
	free(args);
	return (0);
}

/*
 * Connect to a named pipe, retrying briefly while it is busy.
 * Returns the pipe handle, or INVALID_HANDLE_VALUE on failure.
 */
static HANDLE
connect_pipe(const char *name, DWORD access)
{
	HANDLE h = INVALID_HANDLE_VALUE;
	for (int i = 0; i < 20; i++) {
		h = CreateFileA(name, access, 0, NULL, OPEN_EXISTING, 0, NULL);
		if (h != INVALID_HANDLE_VALUE)
			break;
		if (GetLastError() != ERROR_PIPE_BUSY)
			break;
		WaitNamedPipeA(name, 100);
	}
	return (h);
}

/*
 * Wait for a ConnectNamedPipe overlapped operation to complete,
 * also accepting ERROR_PIPE_CONNECTED (client connected before the call).
 * Returns TRUE if connected.
 */
static BOOL
wait_pipe_connect(HANDLE hpipe, OVERLAPPED *ov, HANDLE hprocess)
{
	DWORD ce = GetLastError();
	/*
	 * ce == 0: synchronous success — ConnectNamedPipe blocked and a
	 *          client connected before it returned (non-overlapped pipe).
	 * ce == ERROR_PIPE_CONNECTED: client connected before we called it.
	 * Both mean the pipe is connected; signal the event and return TRUE.
	 */
	if (ce == 0 || ce == ERROR_PIPE_CONNECTED) {
		SetEvent(ov->hEvent);
		return (TRUE);
	}
	if (ce != ERROR_IO_PENDING)
		return (FALSE);
	HANDLE w[2] = { ov->hEvent, hprocess };
	return (WaitForMultipleObjects(2, w, FALSE, 10000) == WAIT_OBJECT_0);
}

/*
 * Called at the very start of main() in every CLI tool.
 * Scans argv for "--zfs-elev-pipe <base>", strips those two entries
 * so the tool never sees them, then connects to both named pipes and
 * redirects all three standard streams through them.
 */
static boolean_t g_is_elev_child = B_FALSE;

boolean_t
windows_is_elev_child(void)
{
	return (g_is_elev_child);
}

void
windows_elevate_child_init(int *argc, char **argv)
{
	const char *base = NULL;

	for (int i = 1; i < *argc - 1; i++) {
		if (strcmp(argv[i], ZFS_ELEVATE_ARG) == 0) {
			base = argv[i + 1];
			for (int j = i; j < *argc - 2; j++)
				argv[j] = argv[j + 2];
			*argc -= 2;
			break;
		}
	}

	if (base == NULL || base[0] == '\0')
		return;

	g_is_elev_child = B_TRUE;

	/* Build the two pipe names from the base */
	char pipe_out[300], pipe_in[300];
	(void) snprintf(pipe_out, sizeof (pipe_out), "%s%s",
	    base, ZFS_ELEVATE_OUT_SFX);
	(void) snprintf(pipe_in, sizeof (pipe_in), "%s%s",
	    base, ZFS_ELEVATE_IN_SFX);

	/* Connect to stdout/stderr pipe (write end) */
	HANDLE hout = connect_pipe(pipe_out, GENERIC_WRITE);
	if (hout == INVALID_HANDLE_VALUE)
		return;

	/* Connect to stdin pipe (read end) */
	HANDLE hin = connect_pipe(pipe_in, GENERIC_READ);
	if (hin == INVALID_HANDLE_VALUE) {
		CloseHandle(hout);
		return;
	}

	SetStdHandle(STD_OUTPUT_HANDLE, hout);
	SetStdHandle(STD_ERROR_HANDLE, hout);
	SetStdHandle(STD_INPUT_HANDLE, hin);

	/*
	 * Redirect the CRT FILE* layer.  The process was started with
	 * SW_HIDE so stdout/stderr/stdin have no valid console handle.
	 * freopen("NUL", ...) gives each FILE* a clean fd in a known-good
	 * state; _dup2 then replaces that fd with one pointing at the pipe.
	 * setvbuf + clearerr must come after: setvbuf internally flushes,
	 * which can re-set _IOERR on a previously-invalid stream.
	 */
	int fd_out = _open_osfhandle((intptr_t)hout, _O_WRONLY | _O_BINARY);
	if (fd_out >= 0) {
		if (freopen("NUL", "w", stdout) != NULL)
			_dup2(fd_out, _fileno(stdout));
		if (freopen("NUL", "w", stderr) != NULL)
			_dup2(fd_out, _fileno(stderr));
	}

	int fd_in = _open_osfhandle((intptr_t)hin, _O_RDONLY | _O_BINARY);
	if (fd_in >= 0) {
		if (freopen("NUL", "r", stdin) != NULL)
			_dup2(fd_in, _fileno(stdin));
	}

	(void) setvbuf(stdout, NULL, _IONBF, 0);
	(void) setvbuf(stderr, NULL, _IONBF, 0);
	(void) setvbuf(stdin,  NULL, _IONBF, 0);
	clearerr(stdout);
	clearerr(stderr);
	clearerr(stdin);
}

/*
 * If not already elevated, relaunch the current process under a UAC
 * "runas" prompt, relay its I/O to/from our console, and exit with its
 * return code.  Returns normally when already elevated.
 */
void
windows_relaunch_elevated(void)
{
	if (geteuid() == 0)
		return;

	fprintf(stderr,
	    "Attempting to relaunch command with"
	    " administrator privileges...\r\n");
	fflush(stderr);

	DWORD pid = GetCurrentProcessId();

	char pipebase[256];
	(void) snprintf(pipebase, sizeof (pipebase),
	    "\\\\.\\pipe\\zfs_elev_%lu", (unsigned long)pid);

	char pipe_out[300], pipe_in[300];
	(void) snprintf(pipe_out, sizeof (pipe_out), "%s%s",
	    pipebase, ZFS_ELEVATE_OUT_SFX);
	(void) snprintf(pipe_in, sizeof (pipe_in), "%s%s",
	    pipebase, ZFS_ELEVATE_IN_SFX);

	/* Create stdout/stderr relay pipe (child writes, parent reads) */
	HANDLE hpipe_out = CreateNamedPipeA(pipe_out,
	    PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
	    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
	    1, 65536, 65536, 0, NULL);

	/* Create stdin relay pipe (parent writes, child reads) */
	HANDLE hpipe_in = CreateNamedPipeA(pipe_in,
	    PIPE_ACCESS_OUTBOUND,
	    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
	    1, 4096, 4096, 0, NULL);

	/* Obtain exe path and original arguments from Windows APIs */
	char exepath[MAX_PATH];
	GetModuleFileNameA(NULL, exepath, sizeof (exepath));

	const char *args = GetCommandLineA();
	if (*args == '"') {
		args++;
		while (*args && *args != '"')
			args++;
		if (*args)
			args++;
	} else {
		while (*args && *args != ' ')
			args++;
	}
	while (*args == ' ')
		args++;

	char full_args[32768];
	if (hpipe_out != INVALID_HANDLE_VALUE &&
	    hpipe_in != INVALID_HANDLE_VALUE) {
		(void) snprintf(full_args, sizeof (full_args),
		    "%s %s %s", args, ZFS_ELEVATE_ARG, pipebase);
	} else {
		(void) snprintf(full_args, sizeof (full_args), "%s", args);
	}

	SHELLEXECUTEINFOA sei = { sizeof (sei) };
	sei.fMask	 = SEE_MASK_NOCLOSEPROCESS;
	sei.lpVerb	 = "runas";
	sei.lpFile	 = exepath;
	sei.lpParameters = full_args;
	sei.nShow	 = SW_HIDE;

	if (!ShellExecuteExA(&sei)) {
		if (hpipe_out != INVALID_HANDLE_VALUE)
			CloseHandle(hpipe_out);
		if (hpipe_in != INVALID_HANDLE_VALUE)
			CloseHandle(hpipe_in);
		(void) fprintf(stderr,
		    "ShellExecuteEx(runas) failed: error %lu\r\n"
		    "This operation requires administrator privileges.\r\n",
		    GetLastError());
		exit(1);
	}

	if (hpipe_out == INVALID_HANDLE_VALUE ||
	    hpipe_in == INVALID_HANDLE_VALUE) {
		/* No pipes: just wait for child to finish with no relay */
		WaitForSingleObject(sei.hProcess, INFINITE);
		DWORD code = 1;
		GetExitCodeProcess(sei.hProcess, &code);
		CloseHandle(sei.hProcess);
		if (hpipe_out != INVALID_HANDLE_VALUE)
			CloseHandle(hpipe_out);
		if (hpipe_in != INVALID_HANDLE_VALUE)
			CloseHandle(hpipe_in);
		exit((int)code);
	}

	/*
	 * Wait for the child to connect to both pipes.
	 * Use overlapped ConnectNamedPipe so we can also watch for the
	 * child process exiting before it connects.
	 */
	OVERLAPPED ov_out_connect = {0};
	ov_out_connect.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	ConnectNamedPipe(hpipe_out, &ov_out_connect);
	if (!wait_pipe_connect(hpipe_out, &ov_out_connect, sei.hProcess)) {
		/* Child didn't connect; fall through without relay */
		goto done;
	}

	OVERLAPPED ov_in_connect = {0};
	ov_in_connect.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	ConnectNamedPipe(hpipe_in, &ov_in_connect);
	if (!wait_pipe_connect(hpipe_in, &ov_in_connect, sei.hProcess)) {
		goto done;
	}

	/*
	 * I/O relay loop.
	 *
	 * We wait on at most two objects:
	 *   w[0] ov_read.hEvent  — overlapped read on child's stdout pipe
	 *   w[1] sei.hProcess    — child process (to detect exit)
	 *
	 * stdin is relayed by a background thread (stdin_relay_thread) rather
	 * than via WaitForMultipleObjects.  Mixing a console input handle into
	 * WMFO causes spurious wake-ups from mouse/focus/key-up events; the
	 * subsequent blocking ReadFile then deadlocks because those non-
	 * character events do not satisfy cooked-mode input.
	 *
	 * The thread is started only after the child prints its first byte of
	 * output, ensuring any password prompt appears before we accept input.
	 * Console echo is disabled at that point so the passphrase is hidden.
	 *
	 * When the child exits: closing hpipe_in causes the thread's next
	 * WriteFile to fail and the thread exits.  If the thread is blocked
	 * in ReadFile(hstdin) at that point it is killed when this process
	 * calls exit() after the relay.
	 */
	{
		OVERLAPPED ov_read = {0};
		ov_read.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

		char out_buf[4096];
		DWORD nread;

		/* Issue the first overlapped read on the stdout pipe */
		if (!ReadFile(hpipe_out, out_buf, sizeof (out_buf),
		    &nread, &ov_read)) {
			if (GetLastError() != ERROR_IO_PENDING)
				goto relay_done;
		}

		HANDLE hstdin = GetStdHandle(STD_INPUT_HANDLE);
		BOOL process_done = FALSE;
		BOOL stdin_active = FALSE;
		HANDLE hthread = NULL;

		/*
		 * Save the parent console's echo mode so we can disable it
		 * while relaying stdin (password entry) and restore afterwards.
		 */
		DWORD console_mode_save = 0;
		BOOL  console_echo_saved = FALSE;
		if (hstdin != INVALID_HANDLE_VALUE &&
		    GetConsoleMode(hstdin, &console_mode_save)) {
			console_echo_saved = TRUE;
		}

		for (;;) {
			HANDLE w[2];
			DWORD  wcount;

			w[0] = ov_read.hEvent;
			if (!process_done) {
				w[1] = sei.hProcess;
				wcount = 2;
			} else {
				wcount = 1;
			}

			DWORD wr = WaitForMultipleObjects(wcount, w,
			    FALSE, INFINITE);

			if (wr == WAIT_OBJECT_0) {
				/* Data (or EOF) on child's stdout pipe */
				if (!GetOverlappedResult(hpipe_out,
				    &ov_read, &nread, FALSE)) {
					/* Broken pipe,c hild closed stdout */
					break;
				}
				if (nread > 0) {
					(void) fwrite(out_buf, 1, nread,
					    stdout);
					(void) fflush(stdout);
					/*
					 * Child has printed something.
					 * Start the stdin relay thread if not
					 * already running.
					 * Disable console echo first so typed
					 * passphrases are not visible.
					 */
					if (!stdin_active &&
					    hstdin != INVALID_HANDLE_VALUE &&
					    hpipe_in != INVALID_HANDLE_VALUE) {
						stdin_active = TRUE;
						if (console_echo_saved) {
							SetConsoleMode(hstdin,
							    console_mode_save &
							    ~ENABLE_ECHO_INPUT);
						}
						stdin_relay_args_t *args =
						    malloc(sizeof (*args));
						if (args != NULL) {
							args->hstdin  = hstdin;
							args->hpipe_in =
							    hpipe_in;
							hthread = CreateThread(
							    NULL, 0,
							    stdin_relay_thread,
							    args, 0, NULL);
							if (hthread == NULL)
								free(args);
						}
					}
				}
				/* Re-issue read */
				ResetEvent(ov_read.hEvent);
				if (!ReadFile(hpipe_out, out_buf,
				    sizeof (out_buf), &nread, &ov_read)) {
					if (GetLastError() != ERROR_IO_PENDING)
						break;
				}
			} else if (wr == WAIT_OBJECT_0 + 1) {
				/*
				 * Child process exited.  Close hpipe_in so the
				 * stdin relay thread's WriteFile fails and the
				 * thread exits.  Continue draining stdout.
				 */
				process_done = TRUE;
				if (hpipe_in != INVALID_HANDLE_VALUE) {
					CloseHandle(hpipe_in);
					hpipe_in = INVALID_HANDLE_VALUE;
				}
			} else {
				break;
			}
		}

		/* Restore console echo */
		if (console_echo_saved)
			SetConsoleMode(hstdin, console_mode_save);

		/*
		 * Release the thread handle.  The thread may still be blocked
		 * in ReadFile(hstdin); it will be killed when this process
		 * calls exit() below.
		 */
		if (hthread != NULL)
			CloseHandle(hthread);

relay_done:
		CloseHandle(ov_read.hEvent);
	}

done:
	CloseHandle(ov_out_connect.hEvent);
	CloseHandle(ov_in_connect.hEvent);
	if (hpipe_out != INVALID_HANDLE_VALUE)
		CloseHandle(hpipe_out);
	if (hpipe_in != INVALID_HANDLE_VALUE)
		CloseHandle(hpipe_in);

	DWORD code = 1;
	WaitForSingleObject(sei.hProcess, INFINITE);
	GetExitCodeProcess(sei.hProcess, &code);
	CloseHandle(sei.hProcess);
	exit((int)code);
}

/*
 * Elevate if ret is non-zero, perm_err is true, and we are not already
 * running as an elevated child.  This is the single call-site helper for
 * CLI tools: they compute perm_err from the libzfs error code (which
 * requires libzfs.h, unavailable here) and pass it in as a boolean.
 */
void
windows_elevate_if_needed(int ret, boolean_t perm_err)
{
	if (ret != 0 && perm_err && !windows_is_elev_child())
		windows_relaunch_elevated();
}
