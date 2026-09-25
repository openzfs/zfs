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
