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
# Copyright (c) 2015 by Lawrence Livermore National Security, LLC.
# All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify 'ulimit -f' file size restrictions are enforced.
#
# STRATEGY:
# 1. Set ulimit file size to unlimited, verify files can be created.
# 2. Reduce ulimit file size, verify the expected error is returned.
#

verify_runnable "both"

log_assert "Verify 'ulimit -f' maximum file size is enforced"

# Verify 'ulimit -f unlimited' works
log_must ulimit -f unlimited
log_must sh -c 'dd if=/dev/zero of=$TESTDIR/ulimit_write_file bs=1M count=2'
log_must sh -c 'truncate -s2M $TESTDIR/ulimit_trunc_file'
log_must rm $TESTDIR/ulimit_write_file $TESTDIR/ulimit_trunc_file

# Verify 'ulimit -f <size>' works
log_must ulimit -f 1024
log_mustnot sh -c 'dd if=/dev/zero of=$TESTDIR/ulimit_write_file bs=1M count=2'
log_must rm $TESTDIR/ulimit_write_file
# FreeBSD allows the sparse file because space has not been allocated.
if ! is_freebsd; then
	log_mustnot sh -c 'truncate -s2M $TESTDIR/ulimit_trunc_file'
	log_must rm $TESTDIR/ulimit_trunc_file
fi

log_pass "Successfully enforced 'ulimit -f' maximum file size"
