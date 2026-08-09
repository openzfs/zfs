#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
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
