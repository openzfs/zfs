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
# TinyZAP add / remove / re-add sanity.
# Tests the memset(TZE_PHYS, 0, chunk_size) zero-on-delete path.
# Re-adding must succeed and the entry must be accessible.
#
# STRATEGY:
# 1. mkdir + touch 51-char name  -> tinyzap
# 2. touch 5 short entries
# 3. rm one short entry          -> assert gone, count = 5
# 4. re-touch removed entry      -> assert present, count = 6
# 5. assert tinyzap throughout
# 6. assert all 6 entries accessible
#

verify_runnable "global"
TDIR="$TESTDIR/zap-tiny-add-remove"

function cleanup { rm -rf "$TDIR"; }
log_onexit cleanup

log_assert "TinyZAP: add / remove / re-add preserves format and entry count"

log_must mkdir "$TDIR"

typeset -r NAME51=$(awk 'BEGIN { s=""; for(i=0;i<51;i++) s=s"r"; print s }')

# Step 1
log_must touch "$TDIR/$NAME51"
zap_assert_type "$TDIR" "tinyzap"
zap_assert_chunk "$TDIR" "128"

# Step 2
typeset i
for i in $(seq 1 5); do
    log_must touch "$TDIR/r$i"
done

# Step 3
log_must rm "$TDIR/r3"
log_mustnot stat "$TDIR/r3"
typeset cnt=$(ls "$TDIR" | wc -l)
[[ $cnt -eq 5 ]] || log_fail "expected 5 entries after rm, got $cnt"

# Step 4
log_must touch "$TDIR/r3"
log_must stat  "$TDIR/r3"
cnt=$(ls "$TDIR" | wc -l)
[[ $cnt -eq 6 ]] || log_fail "expected 6 entries after re-add, got $cnt"

# Step 5
zap_assert_type "$TDIR" "tinyzap"
zap_assert_chunk "$TDIR" "128"

# Step 6
typeset names=("$NAME51")
for i in $(seq 1 5); do names+=("r$i"); done
zap_assert_entries "$TDIR" "${names[@]}"

log_pass "TinyZAP add/remove/re-add passed"

