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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright 2026, tiehexue <tiehexue@hotmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/scheduler/scheduler.cfg

#
# Create a test pool with suitable settings for scheduler benchmarking:
# - compression=off: avoid compression overhead
# - recordsize=128k: match fio block size for aligned I/O
# - atime=off: avoid atime updates during reads
# - xattr=sa: avoid xattr indirection
#
default_raidz_setup_noexit "$DISKS"
log_must zfs set compression=off $TESTPOOL/$TESTFS
log_must zfs set recordsize=$SCHEDULER_BS_HR $TESTPOOL/$TESTFS
log_must zfs set atime=off $TESTPOOL/$TESTFS
log_must zfs set xattr=sa $TESTPOOL/$TESTFS
log_pass
