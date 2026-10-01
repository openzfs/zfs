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
# TinyZAP on-disk format persists across pool export + import for
# both chunk sizes:
#
# Part A (chunk=128):
# Test if mzap_open re-reads mz_flags and rebuilds the
# in-memory zap_t correctly.
#
# Part B (chunk=256):
# Same as Part A but after the chunk is upgraded with chunk_log2=8.
# Verifies the upgraded mz_flags survive on disk.
#
# STRATEGY:
# Part A - chunk=128
# 1. mkdir + touch 51-char name + 5 short  -> tinyzap chunk=128
# 2. zpool export / import
# 3. assert tinyzap, assert all 6 entries accessible
#
# Part B - chunk=256
# 4. touch 120-char name (> TZAP_NAME_LEN(128,8)=116)
#    -> chunk upgrade to 256
# 5. touch 5 more short entries -> assert tinyzap
# 6. zpool export / import
# 7. assert tinyzap, assert all 12 entries accessible
#

verify_runnable "global"
TDIR="$TESTDIR/zap-tiny-remount"

function cleanup {
    poolexists $TESTPOOL || zpool import $TESTPOOL 2>/dev/null
    rm -rf "$TDIR"
}
log_onexit cleanup

log_assert "TinyZAP chunk=128 and chunk=256 both persist across pool export/import"

log_must mkdir "$TDIR"

typeset -r NAME51=$(awk  'BEGIN { s=""; for(i=0;i<51;i++)  s=s"p"; print s }')
typeset -r NAME120=$(awk 'BEGIN { s=""; for(i=0;i<120;i++) s=s"P"; print s }')

# ----------------------------------------------------------------
# Part A: chunk=128 persists across export/import
# ----------------------------------------------------------------
log_note "Part A: TinyZAP chunk=128 export/import"

log_must touch "$TDIR/$NAME51"
typeset i
for i in $(seq 1 5); do
    log_must touch "$TDIR/p$i"
done
zap_assert_type "$TDIR" "tinyzap"
zap_assert_chunk "$TDIR" "128"

log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL

zap_assert_type "$TDIR" "tinyzap"
zap_assert_chunk "$TDIR" "128"

typeset names=("$NAME51")
for i in $(seq 1 5); do names+=("p$i"); done
zap_assert_entries "$TDIR" "${names[@]}"
log_note "Part A passed: chunk=128 survived export/import"

# ----------------------------------------------------------------
# Part B: chunk=256 (after upgrade) persists across export/import
# ----------------------------------------------------------------
log_note "Part B: TinyZAP chunk=256 export/import"

# 120-char key exceeds TZAP_NAME_LEN(128,8)=116 -> chunk upgrade to 256
log_must touch "$TDIR/$NAME120"
zap_assert_type "$TDIR" "tinyzap"
zap_assert_chunk "$TDIR" "256"

for i in $(seq 6 10); do
    log_must touch "$TDIR/p$i"
done
zap_assert_type "$TDIR" "tinyzap"
zap_assert_chunk "$TDIR" "256"

log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL

zap_assert_type "$TDIR" "tinyzap"
zap_assert_chunk "$TDIR" "256"

names+=("$NAME120")
for i in $(seq 6 10); do names+=("p$i"); done
zap_assert_entries "$TDIR" "${names[@]}"
log_note "Part B passed: chunk=256 survived export/import"

log_pass "TinyZAP chunk=128 and chunk=256 remount passed"
