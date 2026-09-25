/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2017 Jorgen Lundman <lundman@lundman.net>
 */

#include <windows.h>
#include <signal.h>

int
sigemptyset(sigset_t *set)
{
	// *set = 0;
	return (0);
}

int
sigfillset(sigset_t *set)
{
	// *set = ~(sigset_t)0;
	return (0);
}

int
sigaddset(sigset_t *set, int sig)
{
	// *set |= (1<<(sig-1));
	return (0);
}

int
sigdelset(sigset_t *set, int sig)
{
	// *set &= ~(1<<(sig-1));
	return (0);
}

int
sigismember(sigset_t *set, int sig)
{
	// return ((*set & (1<<(sig-1))) != 0);
	return (0);
}

int
sigaction(int sig, struct sigaction *sa, struct sigaction *osa)
{
	if (osa)
		osa->sa_handler = signal(sig,
		    (void(__cdecl*)(int))sa->sa_handler);
	else
		signal(sig, (void(__cdecl*)(int))sa->sa_handler);
	return (0);
}

int
sigprocmask(int operation, sigset_t *set, sigset_t *oset)
{
	if (oset)
		/* *oset = 0 */;
	return (0);
}

int
pause(void)
{

}

int
kill(int pid, int sig)
{
	return (0);
}

unsigned int
alarm(unsigned int seconds)
{
	/* No real signal delivery on Windows; never fires. */
	return (0);
}

int
sigpending(sigset_t *set)
{
	if (set)
		set->sig[0] = 0;
	return (0);
}

int
sigsuspend(const sigset_t *mask)
{
	/*
	 * No real signal delivery on Windows: block forever, matching the
	 * rest of this no-op signal emulation. A real implementation would
	 * atomically swap in mask and wait for a signal to arrive.
	 */
	for (;;)
		Sleep(INFINITE);
	return (0);
}

int
sigwait(const sigset_t *set, int *sig)
{
	/*
	 * No real signal delivery on Windows: block forever rather than
	 * return immediately, since callers treat a return here as "the
	 * awaited signal arrived" (e.g. zstream's watchdog thread, which
	 * would otherwise fire immediately at startup instead of never).
	 */
	for (;;)
		Sleep(INFINITE);
	return (0);
}
