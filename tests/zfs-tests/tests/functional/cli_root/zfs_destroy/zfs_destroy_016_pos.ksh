#!/bin/ksh
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

# DESCRIPTION
# Verify zfs destroy test for range of snapshots by giving a list
# of valid and invalid arguments.

# STRATEGY
# 1. Create a list of valid and invalid arguments for range snapshot
#     destroy.
# 2. Set up a filesystem and a volume with multiple snapshots
# 3. Run zfs destroy for all the arguments and verify existence of snapshots
# 4. Verify the destroy for snapshots with clones and hold

. $STF_SUITE/include/libtest.shlib

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && \
	    destroy_dataset $TESTPOOL/$TESTFS1 -R
	datasetexists $TESTPOOL/$TESTVOL && \
	    destroy_dataset $TESTPOOL/$TESTVOL -Rf
}

function setup_snapshots
{
	for i in $snaps; do
		datasetexists $TESTPOOL/$TESTFS1@snap$i && \
		    destroy_dataset $TESTPOOL/$TESTFS1@snap$i
		datasetexists $TESTPOOL/$TESTVOL@snap$i && \
		    destroy_dataset $TESTPOOL/$TESTVOL@snap$i
		log_must zfs snapshot $TESTPOOL/$TESTFS1@snap$i
		log_must zfs snapshot $TESTPOOL/$TESTVOL@snap$i
	done
}

function verify_snapshots
{
	typeset snap_exists=${1:-0}
	if [[ $snap_exists == 1 ]]; then
		for i in $range; do
			snapexists $TESTPOOL/$TESTFS1@snap$i || \
			    log_fail "zfs destroy should not have destroyed" \
			    "$TESTPOOL/$TESTFS1@snap$i"
			snapexists $TESTPOOL/$TESTVOL@snap$i || \
			    log_fail "zfs destroy should not have destroyed" \
			    "$TESTPOOL/$TESTVOL@snap$i"
		done
	else
		for i in $range; do
			snapexists $TESTPOOL/$TESTFS1@snap$i && \
			    log_fail "zfs destroy should have destroyed" \
			    "$TESTPOOL/$TESTFS1@snap$i"
			snapexists $TESTPOOL/$TESTVOL@snap$i && \
			    log_fail "zfs destroy should have destroyed" \
			    "$TESTPOOL/$TESTVOL@snap$i"
		done
	fi
}

invalid_args="@snap0%snap5 @snap1%snap6 @snap0%snap6 @snap5%snap1 \
    @snap1%$TESTPOOL/$TESTFS1@snap5 @snap1%%snap5 @snap1%@snap5 \
    @@snap1%snap5 snap1%snap5 snap1%snap3%snap5"
valid_args="@snap1%snap5 @%"
log_assert "zfs destroy deletes ranges of snapshots"
log_onexit cleanup

log_must zfs create $TESTPOOL/$TESTFS1
log_must zfs create -V $VOLSIZE $TESTPOOL/$TESTVOL
snaps="1 2 3 4 5"
log_note "Verify the valid arguments"
range="1 2 3 4 5"
for args in $valid_args; do
	setup_snapshots
	log_must zfs destroy $TESTPOOL/$TESTFS1$args
	log_must zfs destroy $TESTPOOL/$TESTVOL$args
	verify_snapshots
done

log_note "Verify invalid arguments"
setup_snapshots
for args in $invalid_args; do
	log_mustnot zfs destroy $TESTPOOL/$TESTFS1$args
	log_mustnot zfs destroy $TESTPOOL/$TESTVOL$args
	log_must verify_snapshots 1
done

log_note "Destroy the beginning range"

log_must zfs destroy $TESTPOOL/$TESTFS1@%snap3
log_must zfs destroy $TESTPOOL/$TESTVOL@%snap3
range="1 2 3"
verify_snapshots
range="4 5"
verify_snapshots 1

setup_snapshots
log_note "Destroy the mid range"
log_must zfs destroy $TESTPOOL/$TESTFS1@snap2%snap4
log_must zfs destroy $TESTPOOL/$TESTVOL@snap2%snap4
range="2 3 4"
verify_snapshots
log_must zfs destroy $TESTPOOL/$TESTFS1@snap1%snap5
log_must zfs destroy $TESTPOOL/$TESTVOL@snap1%snap5
range="1 5"
verify_snapshots

setup_snapshots
log_note "Destroy the end range"
log_must zfs destroy $TESTPOOL/$TESTFS1@snap3%
log_must zfs destroy $TESTPOOL/$TESTVOL@snap3%
range="1 2"
verify_snapshots 1
range="3 4 5"
verify_snapshots

setup_snapshots
log_note "Destroy a simple list"
log_must zfs destroy $TESTPOOL/$TESTFS1@snap2,snap4
log_must zfs destroy $TESTPOOL/$TESTVOL@snap2,snap4
range="2 4"
verify_snapshots
range="1 3 5"
verify_snapshots 1

setup_snapshots
log_note "Destroy a list and range together"
log_must zfs destroy $TESTPOOL/$TESTFS1@snap1%snap3,snap5
log_must zfs destroy $TESTPOOL/$TESTVOL@snap1%snap3,snap5
range="1 2 3 5"
verify_snapshots
range=4
verify_snapshots 1

snaps="1 2 3 5 6 7 8 9 10"
setup_snapshots
log_note "Destroy a list of ranges"
log_must zfs destroy $TESTPOOL/$TESTFS1@snap1%snap3,snap5
log_must zfs destroy $TESTPOOL/$TESTVOL@snap1%snap3,snap5
range="1 2 3 5"
verify_snapshots
range=4
verify_snapshots 1

snaps="1 2 3 4 5"
setup_snapshots
log_note "Snapshot destroy with hold"
range="1 2 3 4 5"
for i in 1 2 3 4 5; do
	log_must zfs hold keep $TESTPOOL/$TESTFS1@snap$i
	log_must zfs hold keep $TESTPOOL/$TESTVOL@snap$i
done
log_mustnot zfs destroy $TESTPOOL/$TESTFS1@snap1%snap5
log_mustnot zfs destroy $TESTPOOL/$TESTVOL@snap1%snap5
verify_snapshots 1
for i in 1 2 3 4 5; do
	log_must zfs release keep $TESTPOOL/$TESTFS1@snap$i
	log_must zfs release keep $TESTPOOL/$TESTVOL@snap$i
done
log_must zfs destroy $TESTPOOL/$TESTFS1@snap1%snap5
log_must zfs destroy $TESTPOOL/$TESTVOL@snap1%snap5
verify_snapshots

log_note "Range destroy for snapshots having clones"
setup_snapshots
for i in 1 2 3 4 5; do
	log_must zfs clone $TESTPOOL/$TESTFS1@snap$i $TESTPOOL/$TESTFS1/clone$i
done
log_must zfs destroy -R $TESTPOOL/$TESTFS1@snap1%snap5
log_must zfs destroy $TESTPOOL/$TESTVOL@snap1%snap5
verify_snapshots

log_pass "'zfs destroy' successfully destroys ranges of snapshots"
