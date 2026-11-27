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
# Copyright (c) 2026, Juanmi Taboada <juanmi@juanmitaboada.com>
#

#
# DESCRIPTION:
# A write that cannot be encrypted fails and suspends the pool instead of
# panicking, and zio_resume() returns each time it is called.
#
# STRATEGY:
# 1. In a userland pool (libzpool), write one 16 MiB block of random data
#    to an aes-256-gcm dataset: it must sync.
# 2. The same on an aes-256-ccm dataset, which cannot encrypt 16 MiB in one
#    operation: the pool must suspend, and two calls to zio_resume() must
#    each return and leave the pool suspended again.
#
# The suspended pool lives in the test program's process only, so it goes
# away when the program exits.
#

. $STF_SUITE/include/libtest.shlib

DIR=$TEST_BASE_DIR/zio_encrypt_suspend.$$

function cleanup
{
	rm -rf $DIR
}

log_assert "A write that cannot be encrypted suspends the pool, and" \
	"zio_resume() returns"
log_onexit cleanup

log_must mkdir -p $DIR/gcm $DIR/ccm
log_must timeout 300 zio_encrypt_suspend -d $DIR/gcm -c gcm
log_must timeout 300 zio_encrypt_suspend -d $DIR/ccm -c ccm

log_pass "A write that cannot be encrypted suspends the pool, and" \
	"zio_resume() returns"
