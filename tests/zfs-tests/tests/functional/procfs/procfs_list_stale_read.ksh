#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
# Copyright (c) 2018 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Make sure errors caused by messages being dropped from the list backing the
# procfs file are handled gracefully.
#
# STRATEGY:
# 1. Make sure a few entries have been logged.
# 2. Open the procfs file and start reading from it.
# 3. Write to the file to cause its contents to be dropped.
# 4. Resume reading from the first instance, and check that the expected
#    error is received.
# 5. Repeat steps 1-4, except instead of dropping all the messages by writing
#    to the file, cause enough new messages to be written that the old messages
#    are dropped.
#

function cleanup
{
	echo $default_max_entries >$MAX_ENTRIES_PARAM || log_fail
}

function sync_n
{
	for i in {1..$1}; do
		sync_pool $TESTPOOL
	done
	return 0
}

function do_test
{
	typeset cmd=$1

	# Clear out old entries
	echo 0 >$TXG_HIST || log_fail

	# Add some new entries
	sync_n 20

	# Confirm that there actually is something in the file.
	[[ $(wc -l <$TXG_HIST) -ge 20 ]] || log_fail "expected more entries"

	#
	# Start reading file, pause and run a command that will cause the
	# current offset into the file to become invalid, and then try to
	# finish reading.
	#
	{
		log_must eval "dd bs=512 count=4 >/dev/null"
		log_must eval "$cmd"
		log_must eval 'cat 2>&1 >/dev/null | grep "Input/output error"'
	} <$TXG_HIST
}

typeset -r TXG_HIST=/proc/spl/kstat/zfs/$TESTPOOL/txgs
typeset MAX_ENTRIES_PARAM=/sys/module/zfs/parameters/zfs_txg_history
typeset default_max_entries

log_onexit cleanup

default_max_entries=$(<$MAX_ENTRIES_PARAM) || log_fail
echo 50 >$MAX_ENTRIES_PARAM || log_fail

# Clear all of the existing entries.
do_test "echo 0 >$TXG_HIST"

# Add enough new entries to the list that all of the old ones are dropped.
do_test "sync_n 60"

log_pass "Attempting to read dropped message returns expected error"
