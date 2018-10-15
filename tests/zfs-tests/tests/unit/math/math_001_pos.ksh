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
# Copyright 2018, Richard Elling
#

#
# DESCRIPTION:
# Unit tests for the math.shlib shell functions
#

. ${STF_TOOLS}/include/logapi.shlib
. $STF_SUITE/include/math.shlib

oneTimeTearDown()
{
	log_note completing unit test $0
}

test_within_percent()
{
	assertFalse "cannot divide by zero" "within_percent 0 1 100"
	assertTrue "90% test1" "within_percent 90 100 90"
	assertTrue "90% test2" "within_percent 100 90 90"
	assertFalse "90% miss" "within_percent 90 100 91"
}

test_within_tolerance()
{
	assertTrue "within tolerance" "within_tolerance 10 100 90"
	assertFalse "not within tolerance" "within_tolerance 10 100 89"
}

test_to_bytes()
{
	assertEquals "1 is 1" "1" "$(to_bytes 1)"
	assertEquals "1B is 1" "1" "$(to_bytes 1B)"
	assertEquals "1m converts" "1048576" "$(to_bytes 1m)"
	assertEquals "20mb converts" "20971520" "$(to_bytes 20mb)"
	assertEquals "6423523M converts" "6735552053248" "$(to_bytes 6423523M)"
	assertEquals "7MB converts" "7340032" "$(to_bytes 7MB)"
	assertFalse "cannot convert non numbers" "to_bytes one"
}

. $STF_SUITE/include/shunit2
