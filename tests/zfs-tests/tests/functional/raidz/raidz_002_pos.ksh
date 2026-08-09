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
# Copyright (c) 2016 by Gvozden Neskovic. All rights reserved.
# Use is subject to license terms.
# Copyright (c) 2020 by vStack. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Call the raidz_test tool with sweep to test all supported raidz
#	implementations. This will test several raidz block geometries
#	and several zio parameters that affect raidz block layout. Data
#	reconstruction performs all combinations of failed disks. Wall
#	time is set to 5 min, but actual runtime might be longer.
#

case $((RANDOM % 3)) in
	0)
		# Basic sweep test
		log_must raidz_test -S -t 300
		log_pass "raidz_test parameter sweep test succeeded."
		;;
	1)
		# Using expanded raidz map to test all supported raidz
		# implementations with expanded map and default reflow offset.
		log_must raidz_test -S -e -t 300
		log_pass "raidz_test sweep test with expanded map succeeded."
		;;
	2)
		# Using expanded raidz map ('-e') to test all supported raidz
		# implementations with expanded map and zero reflow offset.
		log_must raidz_test -S -e -r 0 -t 300
		log_pass "raidz_test sweep test with expanded map succeeded."
		;;
	*)
		# avoid shellcheck SC2249
		;;
esac
