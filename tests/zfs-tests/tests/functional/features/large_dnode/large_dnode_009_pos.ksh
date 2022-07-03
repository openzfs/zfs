#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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
# Copyright (c) 2017 by Lawrence Livermore National Security, LLC.
# Use is subject to license terms.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Run many xattrtests on a dataset with large dnodes and xattr=sa to
# stress concurrent allocation of large dnodes.
#

TEST_FS=$TESTPOOL/large_dnode

verify_runnable "both"

function cleanup
{
	datasetexists $TEST_FS && destroy_dataset $TEST_FS
}

log_onexit cleanup
log_assert "xattrtest runs concurrently on dataset with large dnodes"

log_must zfs create $TEST_FS
log_must zfs set dnsize=auto $TEST_FS
log_must zfs set xattr=sa $TEST_FS

for ((i=0; i < 100; i++)); do
	dir="/$TEST_FS/dir.$i"
	log_must mkdir "$dir"

	do_unlink=""
	if [ $((RANDOM % 2)) -eq 0 ]; then
		do_unlink="-k -f 1024"
	else
		do_unlink="-f $((RANDOM % 1024))"
	fi
	log_must eval "xattrtest -R -r -y -x 1 $do_unlink -p $dir >/dev/null 2>&1 &"
done

log_must wait

log_must_busy zpool export $TESTPOOL
log_must zpool import $TESTPOOL
log_must eval "ls -lR /$TEST_FS/ >/dev/null 2>&1"
log_must zdb -d $TESTPOOL
log_pass
