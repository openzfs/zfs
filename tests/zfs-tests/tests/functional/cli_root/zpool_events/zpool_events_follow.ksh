#!/bin/ksh -p
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

#
# Copyright 2017, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_events/zpool_events.kshlib

#
# DESCRIPTION:
# 'zpool events -f' should successfully follow new events.
#
# STRATEGY:
# 1. Clear all ZFS events
# 2. Run 'zpool events -f' in background, saving its output to a temporary file
# 3. Generate some ZFS events
# 4. Verify 'zpool events -f' successfully recorded these new events
#

verify_runnable "both"

function cleanup
{
	[[ -n $pid ]] && kill $pid 2>/dev/null
	rm -f $EVENTS_FILE
}

log_assert "'zpool events -f' should follow new events."
log_onexit cleanup

EVENTS_FILE="$TESTDIR/zpool_events.$$"

# 1. Clear all ZFS events
log_must zpool events -c

# 2. Run 'zpool events -f' in background, saving its output to a temporary file
log_must eval "zpool events -H -f > $EVENTS_FILE &"
pid=$!

# 3. Generate some ZFS events
for i in {1..$EVENTS_NUM}; do
	log_must zpool clear $TESTPOOL
done
# wait a bit to allow the kernel module to process new events
zpool_events_settle

# 4. Verify 'zpool events -f' successfully recorded these new events
EVENTS_LOG=$(wc -l < $EVENTS_FILE)
if [[ $EVENTS_LOG -ne $EVENTS_NUM ]]; then
	log_fail "Unexpected number of events: $EVENTS_LOG != $EVENTS_NUM"
fi

log_pass "'zpool events -f' successfully follows new events."
