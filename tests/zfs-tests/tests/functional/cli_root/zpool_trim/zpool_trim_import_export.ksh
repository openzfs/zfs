#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2019 by Tim Chase. All rights reserved.
# Copyright (c) 2019 Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_trim/zpool_trim.kshlib

#
# DESCRIPTION:
# Trimming automatically resumes across import/export.
#
# STRATEGY:
# 1. Create a one-disk pool.
# 2. Start trimming and verify that trimming is active.
# 3. Export the pool.
# 4. Import the pool.
# 5. Verify that trimming resumes and progress does not regress.
# 6. Suspend trimming.
# 7. Repeat steps 3-4.
# 8. Verify that progress does not regress but trimming is still suspended.
#

function cleanup
{
	if poolexists $TESTPOOL; then
		destroy_pool $TESTPOOL
	fi

	if [[ -d "$TESTDIR" ]]; then
		rm -rf "$TESTDIR"
	fi
}

LARGEFILE="$TESTDIR/largefile"

log_must mkdir "$TESTDIR"
log_must truncate -s 10G "$LARGEFILE"
log_must zpool create -f $TESTPOOL $LARGEFILE

log_must zpool trim -r 256M $TESTPOOL
sleep 2

progress="$(trim_progress $TESTPOOL $LARGEFILE)"
[[ -z "$progress" ]] && log_fail "Trimming did not start"

log_must zpool export $TESTPOOL
log_must zpool import -d $TESTDIR $TESTPOOL

new_progress="$(trim_progress $TESTPOOL $LARGEFILE)"
[[ -z "$new_progress" ]] && log_fail "Trimming did not restart after import"

[[ "$progress" -le "$new_progress" ]] || \
    log_fail "Trimming lost progress after import"
log_mustnot eval "trim_prog_line $TESTPOOL $LARGEFILE | grep suspended"

log_must zpool trim -s $TESTPOOL $LARGEFILE
action_date="$(trim_prog_line $TESTPOOL $LARGEFILE | \
    sed 's/.*ed at \(.*\)).*/\1/g')"
log_must zpool export $TESTPOOL
log_must zpool import -d $TESTDIR $TESTPOOL
new_action_date=$(trim_prog_line $TESTPOOL $LARGEFILE | \
    sed 's/.*ed at \(.*\)).*/\1/g')
[[ "$action_date" != "$new_action_date" ]] && \
    log_fail "Trimming action date did not persist across export/import"

[[ "$new_progress" -le "$(trim_progress $TESTPOOL $LARGEFILE)" ]] || \
	log_fail "Trimming lost progress after import"

log_must eval "trim_prog_line $TESTPOOL $LARGEFILE | grep suspended"

log_pass "Trimming retains state as expected across export/import"
