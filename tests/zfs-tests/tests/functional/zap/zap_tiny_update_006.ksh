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

