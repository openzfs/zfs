#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
#	4. Verify that the snapshot received non-raw in step 3 kept the
#	   guid of its source but was assigned a fresh IV set guid.
#	   This silent divergence is what makes the raw incremental
#	   receives below fail (issue #8758).
#	5. Attempt to raw send the final snapshot of the first dataset
#	   to the other 2 datasets, which should fail.
#	6. Verify that the same raw send is accepted once the IV set
#	   guid check is disabled, and that the raw receive then stamps
#	   the sender's IV set guid onto the new snapshot.
#	7. Repeat steps 1-5, but using bookmarks for incremental sends.
#
#
# A                 B             C     notes
# ------------------------------------------------------------------------------
# snap1 ---raw---> snap1 --raw--> snap1 # all snaps initialized via raw send
# snap2 -non-raw-> snap2 --raw--> snap2 # A sends non-raw to B, B sends raw to C
# snap3 ------------raw---------> snap3 # attempt send to C (should fail)
#


verify_runnable "both"

typeset recverr=$TEST_BASE_DIR/send_mixed_raw.err.$$
typeset recvwarn=$TEST_BASE_DIR/send_mixed_raw.warn.$$

function cleanup
{
    set_tunable32 DISABLE_IVSET_GUID_CHECK 0
    rm -f $recverr $recvwarn
    datasetexists $TESTPOOL/$TESTFS3 && \
        destroy_dataset $TESTPOOL/$TESTFS3 -r
    datasetexists $TESTPOOL/$TESTFS2 && \
        destroy_dataset $TESTPOOL/$TESTFS2 -r
    datasetexists $TESTPOOL/$TESTFS1 && \
        destroy_dataset $TESTPOOL/$TESTFS1 -r
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

# A raw receive preserves both the dataset guid and the IV set guid.
typeset src_guid=$(get_prop guid $TESTPOOL/$TESTFS1@1)
typeset dst_guid=$(get_prop guid $TESTPOOL/$TESTFS2@1)
typeset src_ivset=$(get_prop ivsetguid $TESTPOOL/$TESTFS1@1)
typeset dst_ivset=$(get_prop ivsetguid $TESTPOOL/$TESTFS2@1)
# Guard against get_prop returning "-"/empty, which would make the
# equality/inequality checks below pass vacuously.
[[ -n "$src_ivset" && "$src_ivset" != "-" && \
    -n "$dst_ivset" && "$dst_ivset" != "-" ]] || \
    log_fail "missing IV set guid: '$src_ivset' '$dst_ivset'"
[[ "$src_guid" == "$dst_guid" ]] || \
    log_fail "guid differs after raw receive: $src_guid vs $dst_guid"
[[ "$src_ivset" == "$dst_ivset" ]] || \
    log_fail "ivsetguid differs after raw receive: $src_ivset vs $dst_ivset"

log_must eval "zfs send -i $TESTPOOL/$TESTFS1@1 $TESTPOOL/$TESTFS1@2 |" \
    "zfs receive $TESTPOOL/$TESTFS2 2>$recvwarn"

# The receive above is a non-raw incremental onto a snapshot that was
# itself received raw, so it warns as it happens that it is diverging the
# IV set and will break a later raw incremental (issue #8758).
log_must grep -q "Warning:" $recvwarn

# The matching raw incremental must not warn: it does not diverge anything.
log_must eval "zfs send -w -i $TESTPOOL/$TESTFS2@1 $TESTPOOL/$TESTFS2@2 |" \
    "zfs receive $TESTPOOL/$TESTFS3 2>$recvwarn"
log_mustnot grep -q "Warning:" $recvwarn

# A non-raw incremental receive copies the dataset guid from the send
# stream but generates a fresh IV set guid on the destination. The two
# snapshots look identical to guid-matching tools while their IV sets
# have silently diverged; this is what makes the raw incremental
# receives below fail (issue #8758).
src_guid=$(get_prop guid $TESTPOOL/$TESTFS1@2)
dst_guid=$(get_prop guid $TESTPOOL/$TESTFS2@2)
src_ivset=$(get_prop ivsetguid $TESTPOOL/$TESTFS1@2)
dst_ivset=$(get_prop ivsetguid $TESTPOOL/$TESTFS2@2)
[[ "$src_guid" == "$dst_guid" ]] || \
    log_fail "guid differs after non-raw receive: $src_guid vs $dst_guid"
[[ "$src_ivset" != "$dst_ivset" ]] || \
    log_fail "ivsetguid unexpectedly matches after non-raw receive"

log_mustnot eval "zfs send -w -i $TESTPOOL/$TESTFS1@2 $TESTPOOL/$TESTFS1@3 |" \
    "zfs receive $TESTPOOL/$TESTFS2 2>$recverr"
log_mustnot eval "zfs send -w -i $TESTPOOL/$TESTFS2@2 $TESTPOOL/$TESTFS2@3 |" \
    "zfs receive $TESTPOOL/$TESTFS3"

# The error must name the IV set divergence and the non-raw receive
# that caused it, so that the failure is diagnosable from the message.
log_must grep -q "IV set guid mismatch" $recverr
log_must grep -q "non-raw" $recverr

# The IV set guid check is the only gate: with the check disabled the
# same raw incremental is accepted, and the raw receive stamps the
# sender's IV set guid onto the newly created snapshot.
log_must set_tunable32 DISABLE_IVSET_GUID_CHECK 1
log_must eval "zfs send -w -i $TESTPOOL/$TESTFS1@2 $TESTPOOL/$TESTFS1@3 |" \
    "zfs receive $TESTPOOL/$TESTFS2"
log_must set_tunable32 DISABLE_IVSET_GUID_CHECK 0
src_ivset=$(get_prop ivsetguid $TESTPOOL/$TESTFS1@3)
dst_ivset=$(get_prop ivsetguid $TESTPOOL/$TESTFS2@3)
[[ "$src_ivset" == "$dst_ivset" ]] || \
    log_fail "ivsetguid differs after raw receive: $src_ivset vs $dst_ivset"

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
