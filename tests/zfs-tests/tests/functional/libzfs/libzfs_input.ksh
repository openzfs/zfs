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
# Copyright (c) 2018 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

#
# DESCRIPTION:
#	run C program to test passing different input to libzfs ioctls
#

log_assert "libzfs ioctls handle invalid input arguments"

log_must libzfs_input_check $TESTPOOL

log_pass "libzfs ioctls handle invalid input arguments"
