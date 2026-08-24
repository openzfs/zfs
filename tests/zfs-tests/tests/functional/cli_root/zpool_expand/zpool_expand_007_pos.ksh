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
# Copyright (c) 2026 by Andrew Mochalskyi. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# A vdev grown over the remains of an older pool keeps its own identity
# (issue #16144).
#
# STRATEGY:
# 1) Build a donor pool on a file of the grown size, drive its txg well past
#    anything the pool under test will reach, and keep the two labels that
#    live at the end of the file
# 2) Create a mirror on two smaller files
# 3) Grow one member and write the donor labels where labels 2 and 3 now fall
# 4) "zpool online -e" that member, which must stay healthy, and must leave
#    only its own labels behind
# 5) Plant the donor labels again and import from a cache file, the way the
#    pool comes back after a reboot
# 6) Export and import once more with nothing planted, which is what catches
#    a donor uberblock ring left behind the pool's own config
#
# Labels 2 and 3 sit at offsets relative to the end of a device, so growing
# one puts them on space the pool has never written.  A label left there by
# whoever had the device before is well formed and carries whatever txg it
# was written with, which is what a config is chosen by, so a stale one used
# to be picked and the vdev failed as belonging to a foreign pool.  A mirror
# is used so that a failed member leaves the pool readable and the test can
# report rather than hang.
#

verify_runnable "global"

typeset -r SMALLSZ=$((256 * 1024 * 1024))
typeset -r BIGSZ=$((512 * 1024 * 1024))
typeset -r TAILSZ=$((512 * 1024))
typeset -r TAILAT=$(((BIGSZ - TAILSZ) / TAILSZ))
typeset -r VDEV_A=$TEST_BASE_DIR/expand-stale-a
typeset -r VDEV_B=$TEST_BASE_DIR/expand-stale-b
typeset -r DONOR=$TEST_BASE_DIR/expand-stale-tail
typeset -r CACHE=$TEST_BASE_DIR/expand-stale.cache

function cleanup
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	poolexists $TESTPOOL2 && destroy_pool $TESTPOOL2
	log_must restore_tunable TXG_TIMEOUT
	rm -f $VDEV_A $VDEV_B $DONOR $CACHE $CACHE.boot $CACHE.boot2
}

# The txg a config is chosen by.
function label_txg # file
{
	zdb -l $1 | awk '$1 == "txg:" { print $2; exit }'
}

function plant_donor_labels
{
	log_must dd if=$DONOR of=$VDEV_A bs=$TAILSZ seek=$TAILAT count=1 \
	    conv=notrunc
}

log_must save_tunable TXG_TIMEOUT
log_onexit cleanup

log_assert "a vdev grown over an older pool's labels keeps its own identity"

# The donor's labels only matter while they look newer than the pool's own, and
# an idle pool moves a txg on every zfs_txg_timeout.  Stop that clock, so the
# lead built below is not quietly eaten by however long the run takes and the
# test cannot pass for want of a repro.
log_must set_tunable32 TXG_TIMEOUT 5000

# A pool that used to own the space the member is about to grow into.
log_must truncate -s $BIGSZ $VDEV_A
log_must zpool create $TESTPOOL2 $VDEV_A
typeset -i i=0
while ((i < 25)); do
	log_must zpool sync $TESTPOOL2
	((i = i + 1))
done
# The labels only carry the pool's final txg once it is exported.
log_must zpool export $TESTPOOL2
typeset -i donortxg=$(label_txg $VDEV_A)
log_must dd if=$VDEV_A of=$DONOR bs=$TAILSZ skip=$TAILAT count=1
log_must rm -f $VDEV_A

# The pool under test, on smaller members.
log_must truncate -s $SMALLSZ $VDEV_A
log_must truncate -s $SMALLSZ $VDEV_B
log_must zpool create -o cachefile=$CACHE $TESTPOOL1 mirror $VDEV_A $VDEV_B
typeset -i pooltxg=$(label_txg $VDEV_A)

# The donor labels are only interesting while they look newer than our own.
if ((donortxg <= pooltxg)); then
	log_fail "donor txg $donortxg does not exceed pool txg $pooltxg"
fi
log_note "donor txg $donortxg, pool txg $pooltxg"

# Grow one member onto the donor labels and take the new space.
log_must truncate -s $BIGSZ $VDEV_A
plant_donor_labels
log_must zpool online -e $TESTPOOL1 $VDEV_A

log_must check_state $TESTPOOL1 $VDEV_A "ONLINE"
log_must check_state $TESTPOOL1 "" "ONLINE"

# All four labels are the pool's own again.  zdb prints one block per distinct
# label, tagged with the labels sharing it, so "labels = 0 1 2 3" says both
# that the donor's are gone and that zdb found labels 2 and 3 at the offsets of
# the grown device -- which is the only proof in this test that the member did
# expand.  The pool's size cannot show it: the mirror is still held to the
# smaller member.
log_must zpool sync $TESTPOOL1
log_must eval "zdb -l $VDEV_A | grep -q 'labels = 0 1 2 3'"

# And the same for the import a reboot would do.
log_must cp $CACHE $CACHE.boot
log_must zpool export $TESTPOOL1
plant_donor_labels
log_must zpool import -c $CACHE.boot -o cachefile=$CACHE.boot $TESTPOOL1

log_must check_state $TESTPOOL1 $VDEV_A "ONLINE"
log_must check_state $TESTPOOL1 "" "ONLINE"

# Once more round, with nothing planted this time.  The pool has written its
# own config over the trailing labels by now, so the mismatch that guarded the
# first two imports is gone; if the donor's uberblock rings were left behind
# them the pool would pick one of those and fail to open its rootbp.
log_must zpool sync $TESTPOOL1
log_must cp $CACHE.boot $CACHE.boot2
log_must zpool export $TESTPOOL1
log_must zpool import -c $CACHE.boot2 -o cachefile=$CACHE.boot2 $TESTPOOL1

log_must check_state $TESTPOOL1 $VDEV_A "ONLINE"
log_must check_state $TESTPOOL1 "" "ONLINE"
log_must eval "zdb -l $VDEV_A | grep -q 'labels = 0 1 2 3'"

log_pass "a vdev grown over an older pool's labels keeps its own identity"
