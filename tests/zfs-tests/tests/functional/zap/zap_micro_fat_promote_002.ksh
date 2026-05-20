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
# When a key exceeds TZAP_NAME_LEN(256, 8) = 244 characters, no TinyZAP
# chunk is large enough. The ZAP is upgraded directly from MicroZAP to
# FatZAP.
#
# All pre-existing MicroZAP entries must survive mzap_upgrade().
#
# STRATEGY:
# 1. mkdir + 10 x touch(short) -> assert microzap
# 2. touch 250-char name ->  assert fatzap
# 3. assert all 11 entries accessible
#

verify_runnable "global"
DIR=zap-micro-to-fat
TDIR="$TESTDIR/$DIR"

function cleanup {
	rm -rf "$TDIR";
}
log_onexit cleanup

log_assert "250-char key (> TZAP_NAME_LEN(256,8)=244) forces "\
    "microzap to fatzap upgrade"

log_must mkdir "$TDIR"

# Step 1: fill microzap with 10 short entries
typeset i
for i in $(seq 1 10); do
    log_must touch "$TDIR/e$i"
done

# Step 2: Assert microzap, then add 250-char name to trigger upgrade
zap_assert_type "$TDIR" "microzap"
typeset -r NAME250=$(awk 'BEGIN { s = ""; for (i=0;i<250;i++) s = s "f"; print s }')
log_must touch "$TDIR/$NAME250"

# Step 3: Assert fatzap, then assert all entries accessible
zap_assert_type "$TDIR" "fatzap"
typeset names=()
for i in $(seq 1 10); do names+=("e$i"); done
names+=("$NAME250")
zap_assert_entries "$TDIR" "${names[@]}"

log_pass "MicroZAP -> FatZAP upgrade passed"
