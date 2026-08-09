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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/zfs_promote/zfs_promote.cfg
. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	'zfs promote' can promote a clone filesystem to no longer be dependent
#	on its "origin" snapshot.
#
# STRATEGY:
#	1. Create a snapshot and a clone of the snapshot
#	2. Promote the clone filesystem
#	3. Verify the promoted filesystem become independent
#

verify_runnable "both"

function cleanup
{
	if snapexists $csnap; then
		log_must zfs promote $fs
	fi
	snapexists $snap && destroy_dataset $snap -rR

	typeset data
	for data in $file0 $file1; do
		[[ -e $data ]] && rm -f $data
	done
}

function testing_verify
{
	typeset ds=$1
	typeset ds_file=$2
	typeset snap_file=$3
	typeset c_ds=$4
	typeset c_file=$5
	typeset csnap_file=$6
	typeset origin_prop=""


	snapexists $ds@$TESTSNAP && \
		log_fail "zfs promote cannot promote $ds@$TESTSNAP."
	! snapexists $c_ds@$TESTSNAP && \
		log_fail "The $c_ds@$TESTSNAP after zfs promote doesn't exist."

	origin_prop=$(get_prop origin $ds)
	[[ "$origin_prop" != "$c_ds@$TESTSNAP" ]] && \
		log_fail "The dependency of $ds is not correct."
	origin_prop=$(get_prop origin $c_ds)
	[[ "$origin_prop" != "-" ]] && \
		log_fail "The dependency of $c_ds is not correct."

	if [[ -e $snap_file ]] || [[ ! -e $csnap_file ]]; then
		log_fail "Data file $snap_file cannot be correctly promoted."
	fi
	if [[ ! -e $ds_file ]] || [[ ! -e $c_file ]]; then
		log_fail "There exists data file losing after zfs promote."
	fi

	log_mustnot zfs destroy -r $c_ds
}

log_assert "'zfs promote' can promote a clone filesystem."
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
file0=$TESTDIR/$TESTFILE0
file1=$TESTDIR/$TESTFILE1
snap=$fs@$TESTSNAP
snapfile=$TESTDIR/.zfs/snapshot/$TESTSNAP/$TESTFILE0
clone=$TESTPOOL/$TESTCLONE
cfile=/$clone/$CLONEFILE
csnap=$clone@$TESTSNAP
csnapfile=/$clone/.zfs/snapshot/$TESTSNAP/$TESTFILE0

# setup for promte testing
log_must mkfile $FILESIZE $file0
log_must zfs snapshot $snap
log_must mkfile $FILESIZE $file1
log_must rm -f $file0
log_must zfs clone $snap $clone
log_must mkfile $FILESIZE $cfile

log_must zfs promote $clone
# verify the 'promote' operation
testing_verify $fs $file1 $snapfile $clone $cfile $csnapfile

log_note "Verify 'zfs promote' can change back the dependency relationship."
log_must zfs promote $fs
#verify the result
testing_verify $clone $cfile $csnapfile $fs $file1 $snapfile

log_pass "'zfs promote' reverses the clone parent-child dependency relationship"\
	"as expected."

