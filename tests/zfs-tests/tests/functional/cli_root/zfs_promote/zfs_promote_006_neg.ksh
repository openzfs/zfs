#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	'zfs promote' will fail with invalid arguments:
#	(1) NULL arguments
#	(2) non-existent clone
#	(3) non-clone datasets:
#		pool, fs, snapshot,volume
#	(4) too many arguments.
#	(5) invalid options
#	(6) temporary %recv datasets
#
# STRATEGY:
#	1. Create an array of invalid arguments
#	2. For each invalid argument in the array, 'zfs promote' should fail
#	3. Verify the return code from zfs promote
#

verify_runnable "both"

snap=$TESTPOOL/$TESTFS@$TESTSNAP
clone=$TESTPOOL/$TESTCLONE
recvfs=$TESTPOOL/recvfs
set -A args "" \
	"$TESTPOOL/blah" \
	"$TESTPOOL" "$TESTPOOL/$TESTFS" "$snap" \
	"$TESTPOOL/$TESTVOL" "$TESTPOOL $TESTPOOL/$TESTFS" \
	"$clone $TESTPOOL/$TESTFS" "- $clone" "-? $clone" \
	"$recvfs/%recv"

function cleanup
{
	datasetexists $clone && destroy_dataset $clone

	datasetexists $recvfs && destroy_dataset $recvfs -r

	if snapexists $snap; then
		destroy_snapshot  $snap
	fi
}

log_assert "'zfs promote' will fail with invalid arguments. "
log_onexit cleanup

create_recv_clone $recvfs

typeset -i i=0
while (( i < ${#args[*]} )); do
	log_mustnot zfs promote ${args[i]}

	(( i = i + 1 ))
done

log_pass "'zfs promote' fails with invalid argument as expected."
