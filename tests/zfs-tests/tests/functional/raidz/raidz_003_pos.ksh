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
# Copyright (c) 2020 by vStack. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Call the raidz_test tool with -S and -e to test all supported raidz
#	implementations with expanded map and default reflow offset.
#	This options will test several raidz block geometries and several zio
#	parameters that affect raidz block layout. Data reconstruction performs
#	all combinations of failed disks. Wall time is set to 5min, but actual
#	runtime might be longer.
#

log_must raidz_test -S -e -t 60

log_pass "raidz_test parameter sweep test with expanded map succeeded."
