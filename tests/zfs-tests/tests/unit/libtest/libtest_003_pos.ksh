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
# Unit tests for the libtest.shlib shell functions
# Test for math and other miscellaneous functions
#

. $STF_SUITE/include/libtest.shlib

test_get_max()
{
	assertEquals "arg2 wins" "3" "$(get_max 1 3)"
	assertEquals "arg1 wins" "4" "$(get_max 4 1)"
	assertEquals "multiple args" "5" "$(get_max 1 2 5 3 4)"
}

test_get_min()
{
	assertEquals "arg2 wins" "1" "$(get_min 3 1)"
	assertEquals "arg1 wins" "2" "$(get_min 2 3)"
	assertEquals "multiple args" "1" "$(get_min 4 2 5 3 1)"
}

test_gen_dataset_name()
{
	assertEquals "short name" "abc" "$(gen_dataset_name 1 abc)"
	assertEquals "repeated name" "abcabc" "$(gen_dataset_name 6 abc)"
	assertEquals "long name" \
	    "abc123abc123abc123abc123abc123abc123abc123abc123abc123abc123" \
	    "$(gen_dataset_name 60 abc123)"
}

. $STF_SUITE/include/shunit2
