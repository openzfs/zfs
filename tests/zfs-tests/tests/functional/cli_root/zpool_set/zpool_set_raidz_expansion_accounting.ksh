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
# Copyright (c) 2025, Dimitris Melissourgos. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
# zpool set can modify 'raidz_expansion_accounting' property
#
# STRATEGY:
# 1. Create a pool
# 2. Verify default value is 'legacy'
# 3. Set to 'current' and verify
# 4. Set back to 'legacy' and verify
# 5. Try invalid values and verify they fail
# 6. Export/import and verify property persists
#

verify_runnable "global"

function cleanup
{
	if poolexists $TESTPOOL1; then
		destroy_pool $TESTPOOL1
	fi
	rm -f $disk
}

typeset goodvals=("legacy" "current")
typeset badvals=("off" "on" "0" "1" "none" "default" "invalid")

log_onexit cleanup

log_assert "zpool set can modify 'raidz_expansion_accounting' property"

disk=$TEST_BASE_DIR/disk
log_must mkfile $SIZE $disk
log_must zpool create $TESTPOOL1 $disk

# Verify default value is 'legacy'
typeset value=$(get_pool_prop raidz_expansion_accounting $TESTPOOL1)
if [[ "$value" != "legacy" ]]; then
	log_fail "Default value should be 'legacy' but is '$value'"
fi

# Test setting valid values
for val in ${goodvals[@]}
do
	log_must zpool set raidz_expansion_accounting=$val $TESTPOOL1
	typeset value=$(get_pool_prop raidz_expansion_accounting $TESTPOOL1)
	if [[ "$val" != "$value" ]]; then
		log_fail "'zpool set' did not update value to $val " \
		    "(current = $value)"
	fi
done

# Test that invalid values are rejected
for val in ${badvals[@]}
do
	log_mustnot zpool set raidz_expansion_accounting=$val $TESTPOOL1
done

# Test that property persists across export/import
log_must zpool set raidz_expansion_accounting=current $TESTPOOL1
log_must zpool export $TESTPOOL1
log_must zpool import $TESTPOOL1
typeset value=$(get_pool_prop raidz_expansion_accounting $TESTPOOL1)
if [[ "$value" != "current" ]]; then
	log_fail "Property did not persist across export/import " \
	    "(expected 'current', got '$value')"
fi

log_pass "zpool set can modify 'raidz_expansion_accounting' property"
