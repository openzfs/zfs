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
# Copyright 2026 Colin K. Williams / LINK ORG LLC / LI-NK.SOCIAL. All rights reserved.
#

. $STF_SUITE/tests/functional/zoned_uid/zoned_uid_common.kshlib

#
# DESCRIPTION:
#	Additive least privilege: destructive operations (destroy, rename,
#	clone) require BOTH dsl_deleg grant AND CAP_SYS_ADMIN.
#	CAP_FOWNER alone is insufficient for destructive operations.
#
# STRATEGY:
#	1. Create delegation root with zoned_uid and child datasets
#	2. Grant destroy,rename,clone,mount,create via zfs allow
#	3. With CAP_SYS_ADMIN + destroy grant: destroy succeeds
#	4. With CAP_FOWNER + destroy grant: destroy fails (wrong cap tier)
#	5. With CAP_SYS_ADMIN but no destroy grant: destroy fails (L1 no)
#	6. With no caps + destroy grant: destroy fails (L2 no)
#	7. Test rename similarly: needs SYS_ADMIN + rename grant
#

verify_runnable "global"

function cleanup
{
	zfs destroy -rf "$TESTPOOL/$TESTFS/deleg_root" 2>/dev/null
}

log_assert "Additive L1+L2: destructive ops need dsl_deleg AND CAP_SYS_ADMIN"
log_onexit cleanup

# Setup: delegation root with children.
# Use mountpoint=none so datasets aren't mounted in the host namespace;
# otherwise destroy from a user namespace fails because mount-locked
# mounts (created by the host) cannot be unmounted from a child namespace.
log_must zfs create -o mountpoint=none "$TESTPOOL/$TESTFS/deleg_root"
log_must set_zoned_uid "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID"
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root/victim1"
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root/victim2"
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root/victim3"
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root/victim4"
log_must zfs create "$TESTPOOL/$TESTFS/deleg_root/rename_src"

# Grant destructive permissions
log_must grant_deleg "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID" \
    "create,destroy,rename,clone,mount,snapshot"

# ADD-8: destroy with dsl_deleg + CAP_SYS_ADMIN → allowed
log_note "Test ADD-8: destroy with dsl_deleg + CAP_SYS_ADMIN"
typeset result
result=$(run_in_userns_caps "$ZONED_TEST_UID" "all" \
    destroy "$TESTPOOL/$TESTFS/deleg_root/victim1" 2>&1)
if [[ $? -ne 0 ]]; then
	log_note "Output: $result"
	log_fail "ADD-8: destroy should succeed with dsl_deleg + CAP_SYS_ADMIN"
fi
if datasetexists "$TESTPOOL/$TESTFS/deleg_root/victim1"; then
	log_fail "ADD-8: victim1 should not exist after destroy"
fi
log_note "ADD-8 passed: destroy allowed with SYS_ADMIN"

# ADD-9: destroy with dsl_deleg + CAP_FOWNER → denied (wrong tier)
log_note "Test ADD-9: destroy with dsl_deleg + CAP_FOWNER only"
result=$(run_in_userns_caps "$ZONED_TEST_UID" "drop_sys_admin" \
    destroy "$TESTPOOL/$TESTFS/deleg_root/victim2" 2>&1)
if [[ $? -eq 0 ]]; then
	log_fail "ADD-9: destroy should fail with CAP_FOWNER (needs SYS_ADMIN)"
fi
log_must zfs list "$TESTPOOL/$TESTFS/deleg_root/victim2"
log_note "ADD-9 passed: destroy denied with CAP_FOWNER only"

# ADD-10: destroy with dsl_deleg + no caps → denied
log_note "Test ADD-10: destroy with dsl_deleg + no caps"
result=$(run_in_userns_caps "$ZONED_TEST_UID" "none" \
    destroy "$TESTPOOL/$TESTFS/deleg_root/victim3" 2>&1)
if [[ $? -eq 0 ]]; then
	log_fail "ADD-10: destroy should fail without any capabilities"
fi
log_must zfs list "$TESTPOOL/$TESTFS/deleg_root/victim3"
log_note "ADD-10 passed: destroy denied without caps"

# ADD-11: destroy with CAP_SYS_ADMIN but NO dsl_deleg grant → denied
log_note "Test ADD-11: destroy without dsl_deleg grant"
log_must revoke_deleg "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID"
result=$(run_in_userns_caps "$ZONED_TEST_UID" "all" \
    destroy "$TESTPOOL/$TESTFS/deleg_root/victim4" 2>&1)
if [[ $? -eq 0 ]]; then
	log_fail "ADD-11: destroy should fail without dsl_deleg grant"
fi
log_must zfs list "$TESTPOOL/$TESTFS/deleg_root/victim4"
log_note "ADD-11 passed: destroy denied without dsl_deleg"

# ADD-12: rename with dsl_deleg + CAP_SYS_ADMIN → allowed
log_must grant_deleg "$TESTPOOL/$TESTFS/deleg_root" "$ZONED_TEST_UID" \
    "create,destroy,rename,clone,mount,snapshot"

log_note "Test ADD-12: rename with dsl_deleg + CAP_SYS_ADMIN"
result=$(run_in_userns_caps "$ZONED_TEST_UID" "all" \
    rename "$TESTPOOL/$TESTFS/deleg_root/rename_src" \
    "$TESTPOOL/$TESTFS/deleg_root/rename_dst" 2>&1)
if [[ $? -ne 0 ]]; then
	log_note "Output: $result"
	log_fail "ADD-12: rename should succeed with dsl_deleg + CAP_SYS_ADMIN"
fi
log_must zfs list "$TESTPOOL/$TESTFS/deleg_root/rename_dst"
log_note "ADD-12 passed: rename allowed with SYS_ADMIN"

# ADD-13: clone with dsl_deleg + CAP_FOWNER → denied (destructive tier)
log_note "Test ADD-13: clone with dsl_deleg + CAP_FOWNER"
# Create a snapshot to clone from
log_must zfs snapshot "$TESTPOOL/$TESTFS/deleg_root/victim2@snap"
result=$(run_in_userns_caps "$ZONED_TEST_UID" "drop_sys_admin" \
    clone "$TESTPOOL/$TESTFS/deleg_root/victim2@snap" \
    "$TESTPOOL/$TESTFS/deleg_root/clone_dst" 2>&1)
if [[ $? -eq 0 ]]; then
	log_fail "ADD-13: clone should fail with CAP_FOWNER (needs SYS_ADMIN)"
fi
log_note "ADD-13 passed: clone denied with CAP_FOWNER only"

log_pass "Additive L1+L2: destructive ops need dsl_deleg AND CAP_SYS_ADMIN"
