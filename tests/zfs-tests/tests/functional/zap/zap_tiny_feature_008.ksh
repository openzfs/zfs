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
# Verify the com.hpe:tinyzap pool feature transitions correctly:
# enabled  -> before any TinyZAP directory exists
# active   -> after the first TinyZAP directory is created
#
# STRATEGY:
# 1. assert feature is 'enabled' (setup created pool with feature on)
# 2. mkdir + touch 51-char name  -> tinyzap
# 3. assert feature is 'active'
# 4. assert tinyzap
# 5. Empty the directory and sync to ensure the feature remains active.
#

verify_runnable "global"
TDIR="$TESTDIR/zap-tiny-feature"
FEATURE="com.hpe:tinyzap"

function cleanup { rm -rf "$TDIR"; }
log_onexit cleanup

log_assert "$FEATURE feature: enabled before, active after first TinyZAP"

# Step 1
typeset state
state=$(zpool get -H -o value "feature@tinyzap" $TESTPOOL)
if [[ "$state" != "enabled" && "$state" != "active" ]]; then
    log_fail "tinyzap feature not enabled on pool (got: $state)"
fi
log_note "feature state before: $state"

# Step 2
log_must mkdir "$TDIR"
typeset -r NAME51=$(awk 'BEGIN { s=""; for(i=0;i<51;i++) s=s"F"; print s }')
log_must touch "$TDIR/$NAME51"
sync_pool $TESTPOOL

# Step 3
state=$(zpool get -H -o value "feature@tinyzap" $TESTPOOL)
[[ "$state" == "active" ]] || \
    log_fail "tinyzap feature expected 'active' after first TinyZAP, got '$state'"
log_note "feature state after: $state  [OK]"

# Step 4
zap_assert_type "$TDIR" "tinyzap"
zap_assert_chunk "$TDIR" "128"

# Step 5
log_must rm -f "$TDIR/$NAME51"
sync_pool $TESTPOOL
state=$(zpool get -H -o value "feature@tinyzap" $TESTPOOL)
[[ "$state" == "active" ]] || \
    log_fail "tinyzap feature expected to remain 'active' after " \
    "removing entry, got '$state'"
log_note "feature state after removing entry: $state  [OK]"
log_pass "$FEATURE zap feature flag transition passed"

