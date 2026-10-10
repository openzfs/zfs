#! /bin/ksh -p
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
# Copyright 2026 Oxide Computer Company
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/quota/quota.kshlib

#
# DESCRIPTION:
# ZFS allows a file system to use slightly more space than its quota (the
# last write operation gets "one free hit").  Once in that state, it must
# still be possible to leave the quota alone or to relax (increase) it, even
# to a value that is less than the space in use, since neither operation
# makes the over-quota situation any worse.  Tightening the quota must
# continue to fail.
#
# STRATEGY:
# 1) Apply a quota to the ZFS file system and fill it, so that the space
#	used is slightly more than the quota
# 2) Verify that setting the quota to its current value succeeds
# 3) Verify that increasing the quota succeeds, even when the new value is
#	still less than the space used
# 4) Verify that decreasing the quota still fails
# 5) Verify that removing the quota succeeds
#

verify_runnable "both"

log_assert "Verify that the quota can be relaxed while over quota"

#
# cleanup to be used internally as otherwise quota assertions cannot be
# run independently or out of order
#
function cleanup
{
	[[ -e $TESTDIR/$TESTFILE1 ]] && \
	    log_must rm $TESTDIR/$TESTFILE1

	log_must zfs set quota=none $TESTPOOL/$TESTFS

	wait_freeing $TESTPOOL
	sync_pool $TESTPOOL
}

log_onexit cleanup

#
# Fill the quota.  The write that fills it is allowed to exceed the quota,
# leaving the file system using more space than its quota allows.
#
log_must fill_quota $TESTPOOL/$TESTFS $TESTDIR
sync_pool $TESTPOOL

typeset -i quota=$(get_prop quota $TESTPOOL/$TESTFS)
typeset -i used=$(get_prop used $TESTPOOL/$TESTFS)
log_note "quota=$quota used=$used"

(( used <= quota )) && \
    log_fail "File system is not over quota (used $used, quota $quota)."

# Setting the quota to its current value must succeed (i.e. be idempotent).
log_must zfs set quota=$quota $TESTPOOL/$TESTFS

#
# Relaxing the quota must succeed even though the new value is still less
# than the space used.
#
typeset -i newquota=$(( (quota + used) / 2 ))
if (( newquota > quota && newquota < used )); then
	log_must zfs set quota=$newquota $TESTPOOL/$TESTFS
	log_must eval "[[ $(get_prop quota $TESTPOOL/$TESTFS) -eq $newquota ]]"

	# Tightening the quota while over quota must still fail.
	log_mustnot zfs set quota=$quota $TESTPOOL/$TESTFS
	log_must eval "[[ $(get_prop quota $TESTPOOL/$TESTFS) -eq $newquota ]]"
else
	log_note "Not enough space between the quota and the space used" \
	    "to test an intermediate quota value."
fi

# Removing the quota must succeed.
log_must zfs set quota=none $TESTPOOL/$TESTFS

# With no quota set, a quota below the space used must still be rejected.
log_mustnot zfs set quota=$quota $TESTPOOL/$TESTFS

log_pass "Quota can be relaxed while over quota as expected"
