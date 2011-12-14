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

/* Copyright (c) 2011 Turbo Fredriksson <turbo@bayour.com> */

/* This is a simple debug command to verify that the zfs module
 * retreives values from the iSCSI layer.
 * It will/should output each TID, it's Name and Path of the iSCSI
 * target, one per line. Must be run on the iSCSI target machine.
 * 
 * If you don't get any output (or want to make sure it retreive all
 * nessesary values, such as LUN, block sizes etc), try compiling the
 * iscsi.c file with DEBUG and see what happens.
 *
 *
 * Currently only supports the IET iSCSI target implementation (simply
 * because that's what I run). Feel free to add support for the TGT iSCSI
 * implementation...
 *
 *
 * This command is not built by default since it's only a verification/debug
 * command. To compile it, execute
 *
 *	make iscsi_test
 *
 * in it's directory and run it like this:
 *
 *	./iscsi_test
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <libzfs.h>
#include <libshare.h>
#include "libshare_impl.h"
#include "iscsi.h"

extern boolean_t iscsi_available;
extern iscsi_target_t *iscsi_targets;

int
main(void)
{
	int tid = 0;
	iscsi_target_t *temp = malloc( sizeof(iscsi_target_t) );
#ifdef DEBUG
	char iqn[255];
#endif

	printf("iscsi_available=%d\n", iscsi_available);
	if(iscsi_available) {
		temp = iscsi_targets;
		do
		{
			if(temp->next == NULL)
				break;

			printf("  main tid=%2d => %s ; %s\n",
			       temp->tid, temp->name, temp->path);

			tid = temp->tid;
			temp = temp->next;
		} while (temp != iscsi_targets);
		tid++; /* Next TID is/should be availible */

#ifdef DEBUG
		printf("\nfirst free tid=%d\n", tid);

		iscsi_generate_target("share/test2", iqn, sizeof (iqn));

		if (iscsi_enable_share_one(tid, iqn,
		      "/dev/zvol/share/VirtualMachines/Test",
		      "fileio"))
		{
			printf("ERROR: Failed to create share\n");
			exit(SA_SYSTEM_ERR);
		}

		iscsi_disable_share_one(tid);
#endif

		exit(SA_OK);
	} else
		exit(SA_SYSTEM_ERR);
}
