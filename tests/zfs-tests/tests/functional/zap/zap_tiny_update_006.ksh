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
# TinyZAP value update (overwrite) via zap_update().
#
# STRATEGY:
# 1. mkdir + touch 51-char name  -> tinyzap
# 2. touch fileA
# 3. mv fileB over fileA         -> zap_update replaces dirent value
# 4. assert fileA accessible (points to new inode)
# 5. assert fileB gone
# 6. assert tinyzap
#

verify_runnable "global"
TDIR="$TESTDIR/zap-tiny-update"

function cleanup { rm -rf "$TDIR"; }
log_onexit cleanup

log_assert "TinyZAP: rename overwrites dirent value (zap_update path)"

log_must mkdir "$TDIR"

typeset -r NAME51=$(awk 'BEGIN { s=""; for(i=0;i<51;i++) s=s"u"; print s }')

# Step 1
log_must touch "$TDIR/$NAME51"
zap_assert_type "$TDIR" "tinyzap"
zap_assert_chunk "$TDIR" "128"

# Step 2: create two files
log_must touch "$TDIR/fileA"
log_must touch "$TDIR/fileB"

# Step 3: rename fileB -> fileA  (zap_update on fileA dirent)
log_must mv "$TDIR/fileB" "$TDIR/fileA"

# Step 4: fileA must still be accessible
log_must stat "$TDIR/fileA"

# Step 5: fileB must be gone
log_mustnot stat "$TDIR/fileB"

# Step 6
zap_assert_type "$TDIR" "tinyzap"
zap_assert_chunk "$TDIR" "128"

typeset names=("$NAME51" "fileA")
zap_assert_entries "$TDIR" "${names[@]}"

log_pass "TinyZAP value update (rename overwrite) passed"

