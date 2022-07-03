#!/bin/ksh -p
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
