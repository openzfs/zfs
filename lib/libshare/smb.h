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
 * Copyright (c) 2011 Turbo Fredriksson <turbo@bayour.com>.
 */

/*
 * The maximum SMB share name seems to be 254 characters, though good
 * references are hard to find.
 */

#define	SMB_NAME_MAX		255
#define	SMB_COMMENT_MAX		255

#define	SHARE_DIR		"/var/lib/samba/usershares"
#define	NET_CMD_PATH		"/usr/bin/net"
#define	NET_CMD_ARG_HOST	"127.0.0.1"

typedef struct smb_share_s {
	char name[SMB_NAME_MAX];	/* Share name */
	char path[PATH_MAX];		/* Share path */
	char comment[SMB_COMMENT_MAX];	/* Share's comment */
	boolean_t guest_ok;		/* 'y' or 'n' */

	struct smb_share_s *next;
} smb_share_t;
