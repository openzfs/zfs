#!/bin/ksh -p
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright 2017, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 'zfs diff' should only work with supported options.
#
# STRATEGY:
# 1. Create two snapshots
# 2. Verify every supported option is accepted
# 3. Verify supported options raise an error with unsupported arguments
# 4. Verify other unsupported options raise an error
#

verify_runnable "both"

function cleanup
{
	for snap in $TESTSNAP1 $TESTSNAP2; do
		snapexists "$snap" && destroy_dataset "$snap"
	done
}

log_assert "'zfs diff' should only work with supported options."
log_onexit cleanup

typeset goodopts=("" "-F" "-H" "-t" "-FH" "-Ft" "-Ht" "-FHt")
typeset badopts=("-f" "-h" "-h" "-T" "-Fx" "-Ho" "-tT" "-")

DATASET="$TESTPOOL/$TESTFS"
TESTSNAP1="$DATASET@snap1"
TESTSNAP2="$DATASET@snap2"

# 1. Create two snapshots
log_must zfs snapshot "$TESTSNAP1"
log_must zfs snapshot "$TESTSNAP2"

# 2. Verify every supported option is accepted
for opt in ${goodopts[@]}
do
	log_must zfs diff $opt "$TESTSNAP1"
	log_must zfs diff $opt "$TESTSNAP1" "$DATASET"
	log_must zfs diff $opt "$TESTSNAP1" "$TESTSNAP2"
done

# 3. Verify supported options raise an error with unsupported arguments
for opt in ${goodopts[@]}
do
	log_mustnot zfs diff $opt
	log_mustnot zfs diff $opt "$DATASET"
	log_mustnot zfs diff $opt "$DATASET@noexists"
	log_mustnot zfs diff $opt "$DATASET" "$TESTSNAP1"
	log_mustnot zfs diff $opt "$TESTSNAP2" "$TESTSNAP1"
done

# 4. Verify other unsupported options raise an error
for opt in ${badopts[@]}
do
	log_mustnot zfs diff $opt "$TESTSNAP1" "$DATASET"
	log_mustnot zfs diff $opt "$TESTSNAP1" "$TESTSNAP2"
done

log_pass "'zfs diff' only works with supported options."
