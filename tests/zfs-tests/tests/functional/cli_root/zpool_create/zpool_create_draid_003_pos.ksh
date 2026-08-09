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
# Copyright (c) 2020 Lawrence Livermore National Security, LLC.

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify allowed striped widths (data+parity) and hot spares may be
# configured at pool creation time.
#
# STRATEGY:
# 1) Test valid stripe/spare combinations given the number of children.
# 2) Test invalid stripe/spare/children combinations outside the allow limits.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL

	rm -f $draid_vdevs
	rmdir $TESTDIR
}

log_assert "'zpool create <pool> draid:#d:#c:#s <vdevs>'"

log_onexit cleanup

mkdir $TESTDIR

# Generate 10 random valid configurations to test.
for (( i=0; i<10; i++ )); do
	parity=$(random_int_between 1 3)
	spares=$(random_int_between 0 3)
	data=$(random_int_between 1 16)

	(( min_children = (data + parity + spares) ))
	children=$(random_int_between $min_children 32)

	draid="draid${parity}:${data}d:${children}c:${spares}s"

	draid_vdevs=$(echo $TESTDIR/file.{01..$children})
	log_must truncate -s $MINVDEVSIZE $draid_vdevs

	log_must zpool create $TESTPOOL $draid $draid_vdevs
	log_must poolexists $TESTPOOL
	destroy_pool $TESTPOOL

	rm -f $draid_vdevs
done

children=32
draid_vdevs=$(echo $TESTDIR/file.{01..$children})
log_must truncate -s $MINVDEVSIZE $draid_vdevs

mkdir $TESTDIR
log_must truncate -s $MINVDEVSIZE $draid_vdevs

# Out of order and unknown suffixes should fail.
log_mustnot zpool create $TESTPOOL draid:d8 $draid_vdevs
log_mustnot zpool create $TESTPOOL draid:s3 $draid_vdevs
log_mustnot zpool create $TESTPOOL draid:c32 $draid_vdevs
log_mustnot zpool create $TESTPOOL draid:10x $draid_vdevs
log_mustnot zpool create $TESTPOOL draid:x10 $draid_vdevs

# Exceeds maximum data disks (limited by total children)
log_must zpool create $TESTPOOL draid2:30d $draid_vdevs
log_must destroy_pool $TESTPOOL
log_mustnot zpool create $TESTPOOL draid2:31d $draid_vdevs

# At least one data disk must be requested.
log_mustnot zpool create $TESTPOOL draid2:0d $draid_vdevs

# Check invalid parity levels.
log_mustnot zpool create $TESTPOOL draid0 $draid_vdevs
log_mustnot zpool create $TESTPOOL draid4 $draid_vdevs

# Spares are limited: spares < children - (parity + data).
log_must zpool create $TESTPOOL draid2:20d:10s $draid_vdevs
log_must destroy_pool $TESTPOOL
log_mustnot zpool create $TESTPOOL draid2:20d:11s $draid_vdevs

# The required children argument is enforced.
log_mustnot zpool create $TESTPOOL draid2:0c $draid_vdevs
log_mustnot zpool create $TESTPOOL draid2:31c $draid_vdevs
log_must zpool create $TESTPOOL draid2:32c $draid_vdevs
destroy_pool $TESTPOOL

log_pass "'zpool create <pool> draid:#d:#c:#s <vdevs>'"
