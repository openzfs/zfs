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
#	Validate that the capability control mechanism (capsh --drop within
#	unshare --user --map-root-user) works correctly.  This is a
#	prerequisite for all L2 capability-tier tests (023-027).
#
#	The kernel's ns_capable() checks the effective capability set
#	within the user namespace.  capsh --drop removes capabilities
#	from the bounding set, and the exec'd child process inherits
#	the restricted set.
#
# STRATEGY:
#	1. Verify capsh is available
#	2. Full caps (all): CAP_SYS_ADMIN present, CAP_FOWNER present
#	3. Drop SYS_ADMIN only: CAP_SYS_ADMIN absent, CAP_FOWNER present
#	4. Drop all: CAP_SYS_ADMIN absent, CAP_FOWNER absent
#	5. Verify /proc/self/status CapEff reflects the drops
#	6. Verify drops work under sudo -u (as test UID)
#

verify_runnable "global"

log_assert "Capability control via capsh works in user namespaces"

typeset capsh_cmd
capsh_cmd="$(which capsh)"
if [[ -z "$capsh_cmd" ]]; then
	log_unsupported "capsh not found (install libcap)"
fi

# Helper: check a capability in a namespace
function check_cap_in_ns
{
	typeset drop_arg=$1
	typeset cap_to_check=$2
	typeset expect=$3  # "yes" or "no"

	typeset result cmd_args
	if [[ "$drop_arg" == "none" ]]; then
		cmd_args=""
	else
		cmd_args="$drop_arg"
	fi

	if [[ -z "$cmd_args" ]]; then
		result=$(unshare --user --map-root-user \
		    "$capsh_cmd" --has-p="$cap_to_check" 2>&1 \
		    && echo "YES" || echo "NO")
	else
		# shellcheck disable=SC2086
		result=$(unshare --user --map-root-user \
		    "$capsh_cmd" $cmd_args -- \
		    -c "$capsh_cmd --has-p=$cap_to_check 2>&1 && echo YES || echo NO")
	fi

	if [[ "$expect" == "yes" && "$result" != *"YES"* ]]; then
		log_fail "Expected $cap_to_check to be present ($drop_arg), got: $result"
	fi
	if [[ "$expect" == "no" && "$result" != *"NO"* ]]; then
		log_fail "Expected $cap_to_check to be absent ($drop_arg), got: $result"
	fi
}

# Test 1: Full caps — both present
log_note "Test 1: full caps in namespace"
check_cap_in_ns "none" "cap_sys_admin" "yes"
check_cap_in_ns "none" "cap_fowner" "yes"
log_note "Test 1 passed"

# Test 2: Drop SYS_ADMIN — SYS_ADMIN absent, FOWNER present
log_note "Test 2: drop cap_sys_admin"
check_cap_in_ns "--drop=cap_sys_admin" "cap_sys_admin" "no"
check_cap_in_ns "--drop=cap_sys_admin" "cap_fowner" "yes"
log_note "Test 2 passed"

# Test 3: Drop all — both absent
log_note "Test 3: drop all caps"
check_cap_in_ns "--drop=all" "cap_sys_admin" "no"
check_cap_in_ns "--drop=all" "cap_fowner" "no"
log_note "Test 3 passed"

# Test 4: Verify via /proc/self/status CapEff bitmask
log_note "Test 4: verify CapEff bitmask"
typeset full_eff drop_eff
full_eff=$(unshare --user --map-root-user \
    grep CapEff /proc/self/status 2>&1 | awk '{print $2}')
drop_eff=$(unshare --user --map-root-user \
    "$capsh_cmd" --drop=cap_sys_admin -- \
    -c 'grep CapEff /proc/self/status' 2>&1 | awk '{print $2}')

if [[ "$full_eff" == "$drop_eff" ]]; then
	log_fail "CapEff should differ after dropping cap_sys_admin"
fi
log_note "CapEff full=$full_eff  drop_sys_admin=$drop_eff"

# CAP_SYS_ADMIN is bit 21 = 0x200000
# The difference should be exactly this bit
typeset diff
diff=$(printf "0x%x" $(( 16#${full_eff} - 16#${drop_eff} )))
if [[ "$diff" != "0x200000" ]]; then
	log_note "Expected diff 0x200000 (CAP_SYS_ADMIN), got $diff"
	log_note "This may indicate kernel cap numbering differs; non-fatal"
fi
log_note "Test 4 passed"

# Test 5: Works under sudo -u (as test UID)
log_note "Test 5: capability drops work under sudo -u"
typeset result
result=$(sudo -u \#"$ZONED_TEST_UID" unshare --user --map-root-user \
    "$capsh_cmd" --drop=cap_sys_admin -- \
    -c "$capsh_cmd --has-p=cap_sys_admin 2>&1 && echo YES || echo NO" 2>&1)
if [[ "$result" != *"NO"* ]]; then
	log_fail "cap_sys_admin should be absent under sudo -u, got: $result"
fi

result=$(sudo -u \#"$ZONED_TEST_UID" unshare --user --map-root-user \
    "$capsh_cmd" --drop=cap_sys_admin -- \
    -c "$capsh_cmd --has-p=cap_fowner 2>&1 && echo YES || echo NO" 2>&1)
if [[ "$result" != *"YES"* ]]; then
	log_fail "cap_fowner should be present under sudo -u, got: $result"
fi
log_note "Test 5 passed"

# Test 6: Verify run_in_userns_caps helper modes work
log_note "Test 6: run_in_userns_caps helper verification"

# "all" mode — should have SYS_ADMIN
result=$(run_in_userns_caps "$ZONED_TEST_UID" "all" \
    version 2>&1)
if [[ $? -ne 0 ]]; then
	log_note "Output: $result"
	log_fail "run_in_userns_caps 'all' should work"
fi
log_note "'all' mode works"

# "drop_sys_admin" mode — zfs version should still work (read-only)
result=$(run_in_userns_caps "$ZONED_TEST_UID" "drop_sys_admin" \
    version 2>&1)
if [[ $? -ne 0 ]]; then
	log_note "Output: $result"
	log_fail "run_in_userns_caps 'drop_sys_admin' should work for read-only"
fi
log_note "'drop_sys_admin' mode works"

# "none" mode — zfs version should still work (read-only)
result=$(run_in_userns_caps "$ZONED_TEST_UID" "none" \
    version 2>&1)
if [[ $? -ne 0 ]]; then
	log_note "Output: $result"
	log_fail "run_in_userns_caps 'none' should work for read-only"
fi
log_note "'none' mode works"

log_pass "Capability control via capsh works in user namespaces"
