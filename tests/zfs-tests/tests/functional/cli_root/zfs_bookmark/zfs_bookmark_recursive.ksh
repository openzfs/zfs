#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
#

verify_runnable "both"

typeset TESTSNAP="testsnap"
typeset TESTBM="testbm"
typeset TESTBM2="testbm2"

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

log_pass "'zfs bookmark -r' creates recursive bookmarks as expected"
