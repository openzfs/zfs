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
