#!/bin/ksh -p
#
# CDDL HEADER START
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
# CDDL HEADER END
#
#
# Copyright (c) 2019 Datto, Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Verify that 'zfs receive' produces an error when mixing
#	raw and non-raw sends in a way that would break IV set
#	consistency.
#
# STRATEGY:
#	1. Create an initial dataset with 3 snapshots.
#	2. Perform a raw send of the first snapshot to 2 other datasets.
#	3. Perform a non-raw send of the second snapshot to one of
#	   the other datasets. Perform a raw send from this dataset to
#	   the last one.
#	4. Attempt to raw send the final snapshot of the first dataset
#	   to the other 2 datasets, which should fail.
#	5. Repeat steps 1-4, but using bookmarks for incremental sends.
#
#
# A                 B             C     notes
# ------------------------------------------------------------------------------
# snap1 ---raw---> snap1 --raw--> snap1 # all snaps initialized via raw send
# snap2 -non-raw-> snap2 --raw--> snap2 # A sends non-raw to B, B sends raw to C
# snap3 ------------raw---------> snap3 # attempt send to C (should fail)
#


verify_runnable "both"

function cleanup
{
    datasetexists $TESTPOOL/$TESTFS3 && \
        log_must zfs destroy -r $TESTPOOL/$TESTFS3
    datasetexists $TESTPOOL/$TESTFS2 && \
        log_must zfs destroy -r $TESTPOOL/$TESTFS2
    datasetexists $TESTPOOL/$TESTFS1 && \
        log_must zfs destroy -r $TESTPOOL/$TESTFS1
}
log_onexit cleanup

log_assert "Mixing raw and non-raw receives should fail"

typeset passphrase="password"

log_must eval "echo $passphrase | zfs create -o encryption=on" \
    "-o keyformat=passphrase  $TESTPOOL/$TESTFS1"

log_must zfs snapshot $TESTPOOL/$TESTFS1@1
log_must touch /$TESTPOOL/$TESTFS1/a
log_must zfs snapshot $TESTPOOL/$TESTFS1@2
log_must touch /$TESTPOOL/$TESTFS1/b
log_must zfs snapshot $TESTPOOL/$TESTFS1@3

# Testing with snapshots
log_must eval "zfs send -w $TESTPOOL/$TESTFS1@1 |" \
    "zfs receive $TESTPOOL/$TESTFS2"
log_must eval "echo $passphrase | zfs load-key $TESTPOOL/$TESTFS2"
log_must eval "zfs send -w $TESTPOOL/$TESTFS2@1 |" \
    "zfs receive $TESTPOOL/$TESTFS3"
log_must eval "echo $passphrase | zfs load-key $TESTPOOL/$TESTFS3"

log_must eval "zfs send -i $TESTPOOL/$TESTFS1@1 $TESTPOOL/$TESTFS1@2 |" \
    "zfs receive $TESTPOOL/$TESTFS2"
log_must eval "zfs send -w -i $TESTPOOL/$TESTFS2@1 $TESTPOOL/$TESTFS2@2 |" \
    "zfs receive $TESTPOOL/$TESTFS3"

log_mustnot eval "zfs send -w -i $TESTPOOL/$TESTFS1@2 $TESTPOOL/$TESTFS1@3 |" \
    "zfs receive $TESTPOOL/$TESTFS2"
log_mustnot eval "zfs send -w -i $TESTPOOL/$TESTFS2@2 $TESTPOOL/$TESTFS2@3 |" \
    "zfs receive $TESTPOOL/$TESTFS3"

log_must zfs destroy -r $TESTPOOL/$TESTFS3
log_must zfs destroy -r $TESTPOOL/$TESTFS2

# Testing with bookmarks
log_must zfs bookmark $TESTPOOL/$TESTFS1@1 $TESTPOOL/$TESTFS1#b1
log_must zfs bookmark $TESTPOOL/$TESTFS1@2 $TESTPOOL/$TESTFS1#b2

log_must eval "zfs send -w $TESTPOOL/$TESTFS1@1 |" \
    "zfs receive $TESTPOOL/$TESTFS2"
log_must eval "echo $passphrase | zfs load-key $TESTPOOL/$TESTFS2"

log_must zfs bookmark $TESTPOOL/$TESTFS2@1 $TESTPOOL/$TESTFS2#b1

log_must eval "zfs send -w $TESTPOOL/$TESTFS2@1 |" \
    "zfs receive $TESTPOOL/$TESTFS3"
log_must eval "echo $passphrase | zfs load-key $TESTPOOL/$TESTFS3"

log_must eval "zfs send -i $TESTPOOL/$TESTFS1#b1 $TESTPOOL/$TESTFS1@2 |" \
    "zfs receive $TESTPOOL/$TESTFS2"
log_must eval "zfs send -w -i $TESTPOOL/$TESTFS2#b1 $TESTPOOL/$TESTFS2@2 |" \
    "zfs receive $TESTPOOL/$TESTFS3"

log_mustnot eval "zfs send -w -i $TESTPOOL/$TESTFS1#b2" \
    "$TESTPOOL/$TESTFS1@3 | zfs receive $TESTPOOL/$TESTFS2"
log_mustnot eval "zfs send -w -i $TESTPOOL/$TESTFS2#b2" \
    "$TESTPOOL/$TESTFS2@3 | zfs receive $TESTPOOL/$TESTFS3"

log_pass "Mixing raw and non-raw receives fail as expected"
