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
# Copyright (c) 2018 by Lawrence Livermore National Security, LLC.
# Use is subject to license terms.
#

# DESCRIPTION:
# Verify zed.rc ZED_SYSLOG_SUBCLASS_INCLUDE/EXCLUDE event filtering works.
#
# STRATEGY:
# 1. Execute zpool sub-commands on a pool.
# 2. Test different combinations of ZED_SYSLOG_SUBCLASS_INCLUDE filtering.
# 3. Execute zpool sub-commands on a pool.
# 4. Test different combinations of ZED_SYSLOG_SUBCLASS_EXCLUDE filtering.

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/events/events_common.kshlib

verify_runnable "both"

function cleanup
{
	log_must zed_stop
	zed_rc_restore $zedrc_backup
}

log_assert "Verify zpool sub-commands generate expected events"
log_onexit cleanup

log_must zpool events -c
log_must zed_stop
log_must zed_start

# Backup our zed.rc
zedrc_backup=$(zed_rc_backup)

log_note "Include a single event type"
zed_rc_set ZED_SYSLOG_SUBCLASS_INCLUDE history_event
run_and_verify -p "$TESTPOOL"  -e "sysevent.fs.zfs.history_event" \
    "zfs set compression=off $TESTPOOL/$TESTFS"

log_note "Include a single event type with wildcards"
zed_rc_set ZED_SYSLOG_SUBCLASS_INCLUDE '*history_event*'
run_and_verify -p "$TESTPOOL"  -e "sysevent.fs.zfs.history_event" \
    "zfs set compression=off $TESTPOOL/$TESTFS"

log_note "Test a filter of a non-match and a match"
zed_rc_set ZED_SYSLOG_SUBCLASS_INCLUDE 'foobar|*history_event*'
run_and_verify -p "$TESTPOOL"  -e "sysevent.fs.zfs.history_event" \
    "zfs set compression=off $TESTPOOL/$TESTFS"

log_note "Include multiple events"
zed_rc_set ZED_SYSLOG_SUBCLASS_INCLUDE 'scrub_start|scrub_finish'
run_and_verify -p "$TESTPOOL"  -e "sysevent.fs.zfs.scrub_start" \
    -e "sysevent.fs.zfs.scrub_finish" \
    "zpool scrub $TESTPOOL && wait_scrubbed $TESTPOOL"

# We can't use run_and_verify() for exclusions, so run the rest of the tests
# manually.
log_note "Test one exclusion"
zed_rc_set ZED_SYSLOG_SUBCLASS_EXCLUDE 'history_event'
truncate -s 0 $ZED_DEBUG_LOG
log_must zfs set compression=off $TESTPOOL/$TESTFS
log_must file_wait $ZED_DEBUG_LOG 3
log_mustnot grep -q history_event $ZED_DEBUG_LOG

log_note "Test one exclusion with wildcards"
zed_rc_set ZED_SYSLOG_SUBCLASS_EXCLUDE '*history_event*'
truncate -s 0 $ZED_DEBUG_LOG
log_must zfs set compression=off $TESTPOOL/$TESTFS
log_must file_wait $ZED_DEBUG_LOG 3
log_mustnot grep -q history_event $ZED_DEBUG_LOG

log_note "Test one inclusion and one exclusion"
zed_rc_set ZED_SYSLOG_SUBCLASS_INCLUDE 'scrub_start'
zed_rc_set ZED_SYSLOG_SUBCLASS_EXCLUDE 'scrub_finish'
truncate -s 0 $ZED_DEBUG_LOG
zpool scrub $TESTPOOL
wait_scrubbed $TESTPOOL
log_must file_wait $ZED_DEBUG_LOG 3
log_must grep -q scrub_start $ZED_DEBUG_LOG
log_mustnot grep -q scrub_finish $ZED_DEBUG_LOG

log_pass "zed.rc event filtering works correctly."
