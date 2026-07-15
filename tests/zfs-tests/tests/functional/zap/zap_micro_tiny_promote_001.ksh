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

#
# Copyright (c) 2026, Hewlett Packard Enterprise Development LP.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/zap/zap_common.kshlib

#
# DESCRIPTION:
# Create a directory and add entries to it's max limit and
# check if it stays microzap and then add additional entries
# and check if it's promoted to tinyzap.
#
# STRATEGY:
# 1. Create a directory and add 15 entries with short names (8 chars),
#    filling the MicroZAP to its theoretical maximum of 15 slots.
# 2. Sync and assert ZAP type = microzap.
# 3. Assert all 15 entries are accessible via stat(1).
# 4. Add a 16th entry with a 51-char name (> MZAP_NAME_LEN=49).
# 5. Sync and assert ZAP type = tinyzap.
# 6. Assert all 16 entries (15 short + 1 long) are accessible.
#

verify_runnable "global"
DIR=zap_dir
TDIR="$TESTDIR/$DIR"

function cleanup
{
    rm -rf "$TDIR"
}
log_onexit cleanup

log_assert "MicroZAP: stays microzap at max $MZAP_MAX_ENTRIES entries;" \
    "51-char key triggers upgrade to tinyzap"

typeset -r MZAP_OBJ_SIZE=1024
typeset -r MZAP_ENT_LEN=64
typeset -r MZAP_MAX_ENTRIES=$(( (MZAP_OBJ_SIZE - MZAP_ENT_LEN) / MZAP_ENT_LEN ))

log_must mkdir $TDIR

# Step 1: fill microzap to its theoretical maximum (15 entries).
typeset -i i=0
log_note "Step 1: adding $MZAP_MAX_ENTRIES short entries to fill MicroZAP"
for i in $(seq 1 $MZAP_MAX_ENTRIES); do
    log_must touch "$TDIR/entry$(printf "%04d" $i)"
done
sync_pool $TESTPOOL

# Step 2: assert ZAP type = microzap.
log_note "Step 2: asserting ZAP type is microzap"
zap_assert_type "$TDIR" "microzap"

# Step 3: assert all 15 entries are accessible via stat(1).
log_note "Step 3: stat all $MZAP_MAX_ENTRIES short entries"
typeset -a names=()
for i in $(seq 1 $MZAP_MAX_ENTRIES); do
    names+=("entry$(printf "%04d" $i)")
done
zap_assert_entries "$TDIR" "${names[@]}"

# Step 4: add a 16th entry with a 51-char name (> MZAP_NAME_LEN=49).
typeset -r TRIGGER_NAME=$(awk 'BEGIN { s = ""; for (i=0;i<51;i++) s = s "m"; print s }')
log_note "Step 4: adding 16th entry with 51-char name to trigger upgrade"
log_must touch "$TDIR/$TRIGGER_NAME"
sync_pool $TESTPOOL

# Step 5: directory must now be TinyZAP.
log_note "Step 5: asserting ZAP type is tinyzap after adding long entry"
zap_assert_type "$TDIR" "tinyzap"
zap_assert_chunk "$TDIR" "128"

# Step 6: all 16 entries must survive the re-encoding.
log_note "Step 6: stat all entries after upgrade to tinyzap"
names+=("$TRIGGER_NAME")
zap_assert_entries "$TDIR" "${names[@]}"

zap_dump "$TDIR"

log_pass "MicroZAP correctly promoted to TinyZAP remain accessible"
