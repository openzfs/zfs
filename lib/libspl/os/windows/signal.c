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
		osa->sa_handler = signal(sig, (void(__cdecl*)(int))sa->sa_handler);
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
