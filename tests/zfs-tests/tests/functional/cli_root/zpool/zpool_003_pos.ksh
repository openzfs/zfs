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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Verify debugging features of zpool such as ABORT and freeze/unfreeze
#	should run successfully.
#
# STRATEGY:
# 1. Create an array containing each zpool options.
# 2. For each element, execute the zpool command.
# 3. Verify it run successfully.
#

function cleanup
{
	unset ZFS_ABORT

	if is_freebsd && [ -n "$old_corefile" ]; then
		sysctl kern.corefile=$old_corefile
	fi

	rm -rf $corepath

	# Don't leave the pool frozen.
	destroy_pool $TESTPOOL
	default_mirror_setup $DISKS
}

verify_runnable "both"

log_assert "Debugging features of zpool should succeed."
log_onexit cleanup

corepath=$TESTDIR/core
corefile=$corepath/zpool.core
if [[ -d $corepath ]]; then
	log_must rm -rf $corepath
fi
log_must mkdir $corepath

log_must eval "zpool -? >/dev/null 2>&1"

if is_global_zone ; then
	log_must zpool freeze $TESTPOOL
else
	log_mustnot zpool freeze $TESTPOOL
	log_mustnot zpool freeze ${TESTPOOL%%/*}
fi

log_mustnot zpool freeze fakepool

if is_linux; then
	echo $corefile >/proc/sys/kernel/core_pattern
	echo 0 >/proc/sys/kernel/core_uses_pid
elif is_freebsd; then
	old_corefile=$(sysctl -n kern.corefile)
	log_must sysctl kern.corefile=$corefile
fi
ulimit -c unlimited

export ASAN_OPTIONS="abort_on_error=1:disable_coredump=0"
export ZFS_ABORT=yes

zpool >/dev/null 2>&1

unset ZFS_ABORT

[[ -f $corefile ]] || log_fail "zpool did not dump core by request."

log_pass "Debugging features of zpool succeed."
