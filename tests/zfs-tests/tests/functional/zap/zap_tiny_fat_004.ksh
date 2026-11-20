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
# Copyright (c) 2026, Hewlett Packard Enterprise Development LP.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/zap/zap_common.kshlib

#
# DESCRIPTION:
# Fill a TinyZAP to its exact slot capacity (num_chunks), then add a
# 250-char key which exceeds TZAP_NAME_LEN(256,8)=244, triggering
# mzap_upgrade to convert the TinyZAP to a FatZAP.
#
# All pre-existing TinyZAP entries must survive mzap_upgrade().
#
# STRATEGY:
# 1. mkdir + touch 51-char name  -> tinyzap chunk=128
# 2. read nchunks live from zdb
# 3. fill remaining (nchunks - 1) slots with short names
# 4. assert tinyzap is exactly full (num_entries == nchunks)
# 5. touch 250-char name (> TZAP_NAME_LEN(256,8)=244)
#    -> tzap_try_chunk_upgrade B_FALSE -> mzap_upgrade -> fatzap
# 6. assert fatzap
# 7. assert all entries accessible
#

verify_runnable "global"
TDIR="$TESTDIR/zap-tiny-to-fat"

function cleanup { rm -rf "$TDIR"; }
log_onexit cleanup

log_assert "TinyZAP: fill to num_chunks capacity, exhaust tinyzap, " \
    "add 250-char key trigger upgrade to fatzap; all entries survive"

log_must mkdir "$TDIR"

typeset -r NAME51=$(awk  'BEGIN { s=""; for(i=0;i<51;i++)  s=s"t"; print s }')
typeset -r NAME250=$(awk 'BEGIN { s=""; for(i=0;i<250;i++) s=s"f"; print s }')

# Step 1: promote MicroZAP -> TinyZAP chunk=128
log_must touch "$TDIR/$NAME51"
zap_assert_type  "$TDIR" "tinyzap"
zap_assert_chunk "$TDIR" "128"

# Step 2: read live nchunks from zdb
typeset obj nchunks
obj=$(zap_get_obj "$TDIR")
[[ -z "$obj" ]] && log_fail "could not resolve DMU object for $TDIR"

nchunks=$(zdb -ddddddd "$TESTPOOL/$TESTFS" "$obj" 2>/dev/null | \
    awk '/tinyzap:/ { line = $0; sub(/.*num_chunks=/, "", line); \
        sub(/[^0-9].*/, "", line); print line; exit }')
[[ -z "$nchunks" ]] && log_fail "could not read num_chunks from zdb"
log_note "nchunks=$nchunks after initial promote"

# Step 3: fill remaining (nchunks - 1) slots; NAME51 occupies slot 0
typeset i fill=$(( nchunks - 1 ))
log_note "filling $fill more short entries to reach capacity"
for i in $(seq 1 $fill); do
    log_must touch "$TDIR/s$i"
done

# Step 4: verify TinyZAP is exactly full
typeset cnt=$(ls "$TDIR" | wc -l)
log_note "entries at capacity: $cnt  (nchunks=$nchunks)"
[[ $cnt -eq $nchunks ]] || \
    log_fail "expected $nchunks entries at capacity, got $cnt"
zap_assert_type  "$TDIR" "tinyzap"
zap_assert_chunk "$TDIR" "128"

# Step 5: 250-char key exceeds TZAP_NAME_LEN(256,8)=244
#         tzap_try_chunk_upgrade returns B_FALSE -> mzap_upgrade -> fatzap
log_note "adding 250-char key: upgrade to fatzap"
log_must touch "$TDIR/$NAME250"

# Step 6
zap_assert_type "$TDIR" "fatzap"

# Step 7
typeset names=("$NAME51" "$NAME250")
for i in $(seq 1 $fill); do names+=("s$i"); done
zap_assert_entries "$TDIR" "${names[@]}"

log_pass "TinyZAP fill-to-capacity -> FatZAP passed"

