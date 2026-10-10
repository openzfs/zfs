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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/userquota/userquota_common.kshlib

#
# DESCRIPTION:
#       Malformed or out of range numeric ids in {user,group,project}quota
#       property names must be rejected, and must not silently set the
#       quota of another id (e.g. an empty id must not mean uid 0, and
#       ids larger than 32 bits must not wrap).
#
# STRATEGY:
#       1. For each quota property type, try to set a quota using a
#          malformed or out of range numeric id and verify it fails.
#       2. Verify the quotas of the ids those values used to be mistaken
#          for (0, 5, 1000 and, for users and groups, 4294967295) are
#          unchanged.
#

typeset -a types=("userquota" "userobjquota" "groupquota" "groupobjquota"
    "projectquota" "projectobjquota")
typeset -a bad_ids=("" "-1" "+5" " 5" "5 " "0x5" "4294967296" "4294968296"
    "18446744073709551616" "-4294967295")
typeset -a check_ids=("0" "5" "1000" "4294967295")

function cleanup
{
	typeset t id

	for t in "${types[@]}"; do
		for id in "${check_ids[@]}"; do
			zfs set "$t@$id=none" $QFS >/dev/null 2>&1
		done
	done

	log_must cleanup_quota
}

log_onexit cleanup

log_assert "Check malformed numeric ids in user|group|project quota names"

typeset t id

for t in "${types[@]}"; do
	for id in "${bad_ids[@]}"; do
		log_mustnot zfs set "$t@$id=$TEST_QUOTA" $QFS
	done
done

for t in "${types[@]}"; do
	for id in "${check_ids[@]}"; do
		# 4294967295 is not a valid project id
		[[ $t == project* && $id == 4294967295 ]] && continue
		log_must check_quota "$t@$id" $QFS 0
	done
done

log_pass "Malformed numeric ids in user|group|project quota names rejected"
