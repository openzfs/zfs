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
# Copyright (c) 2007, Sun Microsystems Inc. All rights reserved.
# Copyright (c) 2021, Rich Ercolani.
# Use is subject to license terms.
#

. $STF_SUITE/include/properties.shlib
. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Import a pool containing variously-permuted zstd-compressed files,
# then try to copy them out.

typeset TESTPOOL_ZSTD_FILE=$STF_SUITE/tests/functional/compression/testpool_zstd.tar.gz
verify_runnable "both"

function cleanup
{
	destroy_pool testpool_zstd
	rm -f $TEST_BASE_DIR/testpool_zstd
	
}

log_assert "Trying to read data from variously mangled zstd datasets"
log_onexit cleanup

log_must tar --directory $TEST_BASE_DIR -xzSf $TESTPOOL_ZSTD_FILE
log_must zpool import -d $TEST_BASE_DIR testpool_zstd
log_must dd if=/testpool_zstd/x86_64/zstd of=/dev/null
log_must dd if=/testpool_zstd/ppc64_fbsd/zstd of=/dev/null

log_pass "Reading from mangled zstd datasets works as expected."
