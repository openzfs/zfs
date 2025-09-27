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
# Copyright (c) 2025 Klara Inc.
#
# This software was developed by Mariusz Zaborski <oshogbo@FreeBSD.org>
# under sponsorship from Wasabi Technology, Inc. and Klara Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_scrub/zpool_scrub.cfg

#
# DESCRIPTION:
#	Verify that scrub_recent_time is correctly calculated.
#
# STRATEGY:
#	1. Set scrub_recent_time and check if the scrub_recent_time_hours are
#	   scrub_recent_time_day are set correctly.
#	2. Set scrub_recent_time_hours and check if the scrub_recent_time are
#	   scrub_recent_time_day are set correctly.
#	3. Set scrub_recent_time_day and check if the scrub_recent_time are
#	   scrub_recent_time_hours are set correctly.
#

verify_runnable "global"

function cleanup
{
	log_must restore_tunable SCRUB_RECENT_TIME
}

function check_scrub_recent_time
{
	recent_time="${1}"
	recent_time_hours="${2}"
	recent_time_days="${3}"

	[[ "${recent_time}" == "$(get_tunable SCRUB_RECENT_TIME)" ]] || \
		log_fail "scrub_recent_time has wrong value"
	[[ "${recent_time_hours}" == "$(get_tunable SCRUB_RECENT_TIME_HOURS)" ]] || \
		log_fail "scrub_recent_time_hours has wrong value"
	[[ "${recent_time_days}" == "$(get_tunable SCRUB_RECENT_TIME_DAYS)" ]] || \
		log_fail "scrub_recent_time_days has wrong value"
}

log_onexit cleanup

log_assert "Verify "\
    "scrub_recent_time/scrub_recent_time_hours/scrub_recent_time_days is "\
    "calculated correctly."

log_must save_tunable SCRUB_RECENT_TIME

log_must set_tunable64 SCRUB_RECENT_TIME 1
check_scrub_recent_time "1" "0" "0"

log_must set_tunable64 SCRUB_RECENT_TIME "30"
check_scrub_recent_time "30" "0" "0"

log_must set_tunable64 SCRUB_RECENT_TIME "59"
check_scrub_recent_time "59" "0" "0"

log_must set_tunable64 SCRUB_RECENT_TIME "60"
check_scrub_recent_time "60" "0" "0"

log_must set_tunable64 SCRUB_RECENT_TIME "3600"
check_scrub_recent_time "3600" "1" "0"

log_must set_tunable64 SCRUB_RECENT_TIME "86400"
check_scrub_recent_time "86400" "24" "1"

log_must set_tunable64 SCRUB_RECENT_TIME "172800"
check_scrub_recent_time "172800" "48" "2"

log_must set_tunable64 SCRUB_RECENT_TIME "100000"
check_scrub_recent_time "100000" "27" "1"

log_must set_tunable64 SCRUB_RECENT_TIME "200000"
check_scrub_recent_time "200000" "55" "2"

log_must set_tunable64 SCRUB_RECENT_TIME "86399"
check_scrub_recent_time "86399" "23" "0"

log_must set_tunable64 SCRUB_RECENT_TIME "604799"
check_scrub_recent_time "604799" "167" "6"

log_must set_tunable64 SCRUB_RECENT_TIME_HOURS "1"
check_scrub_recent_time "3600" "1" "0"

log_must set_tunable64 SCRUB_RECENT_TIME_HOURS "12"
check_scrub_recent_time "43200" "12" "0"

log_must set_tunable64 SCRUB_RECENT_TIME_HOURS "23"
check_scrub_recent_time "82800" "23" "0"

log_must set_tunable64 SCRUB_RECENT_TIME_HOURS "24"
check_scrub_recent_time "86400" "24" "1"

log_must set_tunable64 SCRUB_RECENT_TIME_HOURS "25"
check_scrub_recent_time "90000" "25" "1"

log_must set_tunable64 SCRUB_RECENT_TIME_HOURS "48"
check_scrub_recent_time "172800" "48" "2"

log_must set_tunable64 SCRUB_RECENT_TIME_HOURS "100"
check_scrub_recent_time "360000" "100" "4"

log_must set_tunable64 SCRUB_RECENT_TIME_DAYS "1"
check_scrub_recent_time "86400" "24" "1"

log_must set_tunable64 SCRUB_RECENT_TIME_DAYS "2"
check_scrub_recent_time "172800" "48" "2"

log_must set_tunable64 SCRUB_RECENT_TIME_DAYS "7"
check_scrub_recent_time "604800" "168" "7"

log_must set_tunable64 SCRUB_RECENT_TIME_DAYS "30"
check_scrub_recent_time "2592000" "720" "30"

log_must set_tunable64 SCRUB_RECENT_TIME_DAYS "365"
check_scrub_recent_time "31536000" "8760" "365"

log_pass "Verified "\
    "scrub_recent_time/scrub_recent_time_hours/scrub_recent_time_days is "\
    "calculated correctly."
