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
# A 51-char name promotes MicroZAP to TinyZAP with chunk=128.
# A subsequent 120-char name (> TZAP_NAME_LEN(128,8)=116) triggers
# chunk upgrade to re-pack in-place with chunk=256.
# Directory stays tinyzap; all entries survive.
#
# STRATEGY:
# 1. mkdir + touch 51-char name  -> microzap promotes to tinyzap chunk=128
# 2. touch 5 short names         -> assert tinyzap
# 3. touch 120-char name         -> -> chunk=256
# 4. assert tinyzap
# 5. assert all 7 entries accessible
#

verify_runnable "global"
TDIR="$TESTDIR/zap-tiny-chunk-upgrade"

function cleanup { rm -rf "$TDIR"; }
log_onexit cleanup

log_assert "TinyZAP chunk 128->256: tzap_try_chunk_upgrade preserves all entries"

log_must mkdir "$TDIR"

typeset -r NAME51=$(awk  'BEGIN { s=""; for(i=0;i<51;i++)  s=s"a"; print s }')
typeset -r NAME120=$(awk 'BEGIN { s=""; for(i=0;i<120;i++) s=s"b"; print s }')

# Step 1: promote MicroZAP -> TinyZAP chunk=128
log_must touch "$TDIR/$NAME51"
zap_assert_type "$TDIR" "tinyzap"
zap_assert_chunk "$TDIR" "128"

# Step 2: more short entries
typeset i
for i in $(seq 1 5); do
    log_must touch "$TDIR/s$i"
done
zap_assert_type "$TDIR" "tinyzap"
zap_assert_chunk "$TDIR" "128"

# Step 3: 120-char key exceeds TZAP_NAME_LEN(128,8)=116
#         tzap_try_chunk_upgrade re-packs with chunk=256
log_must touch "$TDIR/$NAME120"

# Step 4
zap_assert_type "$TDIR" "tinyzap"
zap_assert_chunk "$TDIR" "256"

# Step 5
typeset names=("$NAME51" "$NAME120")
for i in $(seq 1 5); do names+=("s$i"); done
zap_assert_entries "$TDIR" "${names[@]}"

log_pass "TinyZAP chunk upgrade 128->256 passed"

