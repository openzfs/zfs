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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 'zfs bookmark -r' creates a bookmark for the source snapshot of every
# descendant dataset that has it, and skips those that do not.
#
# STRATEGY:
# 1. Recursively snapshot a dataset hierarchy.
# 2. Verify 'zfs bookmark -r' creates a bookmark on every dataset in the
#    subtree, and not on a sibling outside of it.
# 3. Verify a descendant created after the snapshot is skipped (not an error)
#    while the others still get a new bookmark.
# 4. Verify 'zfs bookmark -r' rejects a bookmark source.
# 5. Verify 'zfs bookmark -r' rejects a new bookmark on a dataset other than
#    the source snapshot's, and creates no bookmarks at all.
# 6. Verify the short forms ('#bookmark' and '@snapshot') still work with -r.
#

verify_runnable "both"

typeset TESTSNAP="testsnap"
typeset TESTBM="testbm"
typeset TESTBM2="testbm2"
typeset TESTBM3="testbm3"
typeset TESTBM4="testbm4"

typeset ROOT="$TESTPOOL/$TESTFS"
typeset -a SUBTREE=("$ROOT" "$ROOT/child" "$ROOT/recv")
typeset OUTSIDE="$TESTPOOL/${TESTFS}_with_suffix"
typeset LATE="$ROOT/late"

function cleanup
{
	datasetexists "$LATE" && destroy_dataset "$LATE" "-r"
	for ds in "$ROOT" "$OUTSIDE"; do
		snapexists "$ds@$TESTSNAP" && destroy_dataset "$ds@$TESTSNAP" "-r"
	done
}

log_onexit cleanup

log_assert "'zfs bookmark -r' bookmarks the source snapshot of every " \
    "descendant that has it"

# 1. Recursive snapshot of the subtree only (not the sibling).
log_must zfs snapshot -r "$ROOT@$TESTSNAP"

# Give the sibling a snapshot of the same name so the scoping check below is
# meaningful: it shares the snapshot name and a dataset-name prefix with $ROOT,
# so it would be wrongly bookmarked by a name-prefix match rather than true
# descendant iteration.
log_must zfs snapshot "$OUTSIDE@$TESTSNAP"

# 2. Recursive bookmark across the subtree.
log_must zfs bookmark -r "$ROOT@$TESTSNAP" "$ROOT#$TESTBM"
for ds in "${SUBTREE[@]}"; do
	log_must eval "bkmarkexists $ds#$TESTBM"
done
# The sibling outside the subtree must not be bookmarked, even though it has
# the same snapshot and a matching name prefix.
log_mustnot eval "bkmarkexists $OUTSIDE#$TESTBM"

# 3. A dataset created after the snapshot has no source snapshot and must be
#    skipped without failing the request; the others get the new bookmark.
log_must zfs create "$LATE"
log_must zfs bookmark -r "$ROOT@$TESTSNAP" "$ROOT#$TESTBM2"
for ds in "${SUBTREE[@]}"; do
	log_must eval "bkmarkexists $ds#$TESTBM2"
done
log_mustnot eval "bkmarkexists $LATE#$TESTBM2"

# 4. A bookmark source is not valid with -r.
log_mustnot zfs bookmark -r "$ROOT#$TESTBM" "$ROOT#$TESTBM2"

# 5. The new bookmark must be on the source snapshot's dataset: naming a
#    different dataset (here one that also has the snapshot) must fail and
#    must not create a bookmark anywhere.
log_mustnot zfs bookmark -r "$ROOT@$TESTSNAP" "$OUTSIDE#$TESTBM3"
log_mustnot zfs bookmark -r "$ROOT@$TESTSNAP" "$ROOT/child#$TESTBM3"
for ds in "${SUBTREE[@]}" "$OUTSIDE"; do
	log_mustnot eval "bkmarkexists $ds#$TESTBM3"
done

# 6. The short forms take the dataset from the other argument.
log_must zfs bookmark -r "$ROOT@$TESTSNAP" "#$TESTBM3"
log_must zfs bookmark -r "@$TESTSNAP" "$ROOT#$TESTBM4"
for ds in "${SUBTREE[@]}"; do
	log_must eval "bkmarkexists $ds#$TESTBM3"
	log_must eval "bkmarkexists $ds#$TESTBM4"
done
log_mustnot eval "bkmarkexists $OUTSIDE#$TESTBM3"
log_mustnot eval "bkmarkexists $OUTSIDE#$TESTBM4"

log_pass "'zfs bookmark -r' creates recursive bookmarks as expected"
