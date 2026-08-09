// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */

/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pwd.h>

#define	EXECSHELL	"/bin/sh"

int
main(int argc, char *argv[])
{
	char *plogin = NULL;
	char cmds[BUFSIZ] = { 0 };
	char sep[] = " ";
	struct passwd *ppw = NULL;
	int i, len;

	if (argc < 3 || strlen(argv[1]) == 0) {
		(void) printf("\tUsage: %s <login> <commands> ...\n", argv[0]);
		return (1);
	}

	plogin = argv[1];
	len = 0;
	for (i = 2; i < argc; i++) {
		(void) snprintf(cmds+len, sizeof (cmds)-len,
		    "%s%s", argv[i], sep);
		len += strlen(argv[i]) + strlen(sep);
	}

	if ((ppw = getpwnam(plogin)) == NULL) {
		perror("getpwnam");
		return (errno);
	}
	if (setgid(ppw->pw_gid) != 0) {
		perror("setgid");
		return (errno);
	}
	if (setuid(ppw->pw_uid) != 0) {
		perror("setuid");
		return (errno);
	}

	if (execl(EXECSHELL, "sh",  "-c", cmds, (char *)NULL) != 0) {
		perror("execl: " EXECSHELL);
		return (errno);
	}

	return (0);
}
