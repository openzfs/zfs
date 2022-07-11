#!/bin/ksh -p
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
