#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
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
# CDDL HEADER END
#

#
# Copyright 2026, tiehexue <tiehexue@hotmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/async/async.cfg
. $STF_SUITE/tests/functional/async/async.kshlib

if ! is_linux; then
	log_unsupported "Async read tests require Linux"
fi

if ! command -v fio > /dev/null 2>&1; then
	log_unsupported "fio is required"
fi

if ! fio_ioengine_available "libaio"; then
	log_unsupported "fio libaio ioengine is required"
fi

#
# Async Direct I/O reads are enabled by default.  Save the current value so
# cleanup.ksh can restore it, and force it on so tests run against the async
# path.
#
if tunable_exists ASYNC_DIO_ENABLED; then
	# Remove a stale save file from a previous failed/hung run.
	rm -f "$TEST_BASE_DIR/tunable-ASYNC_DIO_ENABLED"
	log_must save_tunable ASYNC_DIO_ENABLED
	log_must set_tunable32 ASYNC_DIO_ENABLED 1
fi

#
# Create a test pool with Direct I/O friendly settings:
# - compression=off: avoid compression overhead for DIO
# - recordsize=4K: match the fio block size for aligned I/O
# - atime=off: avoid atime updates during reads
# - xattr=sa: avoid xattr indirection
#
default_raidz_setup_noexit "$DISKS"
log_must zfs set compression=off $TESTPOOL/$TESTFS
log_must zfs set recordsize=$ASYNC_BS_HR $TESTPOOL/$TESTFS
log_must zfs set atime=off $TESTPOOL/$TESTFS
log_must zfs set xattr=sa $TESTPOOL/$TESTFS
log_pass
