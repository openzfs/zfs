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

#include <sys/types.h>
#include <regex.h>

/* Fill me in when we want regex support */

int
regexec(const regex_t* restrict preg, const char* restrict string,
    size_t nmatch, regmatch_t pmatch[restrict], int eflags)
{
  return (0);
}

int
regcomp(regex_t* restrict preg, const char* restrict pattern,
    int cflags)
{
	return (0);
}

void
regfree(regex_t* preg)
{

}
