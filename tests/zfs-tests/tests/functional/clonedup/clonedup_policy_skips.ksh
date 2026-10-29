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
# Copyright (c) 2026, Patrick Domack. All rights reserved.
#

. $STF_SUITE/tests/functional/clonedup/clonedup.kshlib

#
# DESCRIPTION:
#	Read-only filesystems, immutable files and files over quota
#	are not rewritten, and the counters say so.  Each case runs
#	alone and counts a skip for every block it refuses.
#

verify_runnable "global"

function cleanup
{
	if is_freebsd; then
		chflags noschg /$TESTPOOL/imm/b 2>/dev/null
	else
		chattr -i /$TESTPOOL/imm/b 2>/dev/null
	fi
	clonedup_cleanup
}

log_assert "policy skips: readonly, immutable, quota"
log_onexit cleanup

clonedup_pool_create
typeset all="$(clonedup_all_blocks)"

log_must zfs create $TESTPOOL/ro
clonedup_write /$TESTPOOL/ro/a
clonedup_dup /$TESTPOOL/ro/a /$TESTPOOL/ro/b
log_must zfs set readonly=on $TESTPOOL/ro

log_must zfs create $TESTPOOL/imm
clonedup_write /$TESTPOOL/imm/a
clonedup_dup /$TESTPOOL/imm/a /$TESTPOOL/imm/b
if is_freebsd; then
	log_must chflags schg /$TESTPOOL/imm/a
	log_must chflags schg /$TESTPOOL/imm/b
else
	log_must chattr +i /$TESTPOOL/imm/a
	log_must chattr +i /$TESTPOOL/imm/b
fi

log_must zfs create $TESTPOOL/quota
clonedup_write /$TESTPOOL/quota/a
clonedup_dup /$TESTPOOL/quota/a /$TESTPOOL/quota/b
log_must chown nobody /$TESTPOOL/quota/a /$TESTPOOL/quota/b
clonedup_sync
log_must zfs set userquota@nobody=512K $TESTPOOL/quota

typeset ds o
for ds in ro imm quota; do
	for o in ro imm quota; do
		if [[ $o == $ds ]]; then
			log_must zfs set clonedup=on $TESTPOOL/$o
		else
			log_must zfs set clonedup=off $TESTPOOL/$o
		fi
	done
	clonedup_run -f
	clonedup_check_shared $TESTPOOL/$ds a $TESTPOOL/$ds b ""
	clonedup_jstat_is clonedup_skipped_policy $CD_BLOCKS
	clonedup_jstat_is errors 0
done
clonedup_leakcheck

log_pass "policy skips: readonly, immutable, quota"
