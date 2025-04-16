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
