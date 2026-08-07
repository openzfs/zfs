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

