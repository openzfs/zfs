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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# With ZFS_ABORT set, all zfs commands should be able to abort and generate a
# core file.
#
# STRATEGY:
# 1. Create an array of zfs command
# 2. Execute each command in the array
# 3. Verify the command aborts and generate a core file
#

verify_runnable "both"

function cleanup
{
	unset ZFS_ABORT

	if is_freebsd && [[ -n $savedcorefile ]]; then
		sysctl kern.corefile=$savedcorefile
	fi

	if [[ -d $corepath ]]; then
		rm -rf $corepath
	fi
	for ds in $fs1 $fs $ctr; do
		if datasetexists $ds; then
			log_must zfs destroy -rRf $ds
		fi
	done
}

log_assert "With ZFS_ABORT set, all zfs commands can abort and generate a " \
    "core file."
log_onexit cleanup

# Preparation work for testing
savedcorefile=""
corepath=$TESTDIR/core
corefile=$corepath/core.zfs
if [[ -d $corepath ]]; then
	rm -rf $corepath
fi
log_must mkdir $corepath

ctr=$TESTPOOL/$TESTCTR
log_must zfs create $ctr

fs=$ctr/$TESTFS
fs1=$ctr/$TESTFS1
snap=$fs@$TESTSNAP
clone=$ctr/$TESTCLONE
streamf=$corepath/s.$$

typeset cmds=("create $fs" "list $fs" "snapshot $snap" "set snapdir=hidden $fs" \
    "get snapdir $fs" "rollback $snap" "inherit snapdir $fs" \
    "rename $fs $fs-new" "rename $fs-new $fs" "unmount $fs" \
    "mount $fs" "share $fs" "unshare $fs" "send $snap \>$streamf" \
    "receive $fs1 \<$streamf" "clone $snap $clone" "promote $clone" \
    "promote $fs" "destroy -rRf $fs")

typeset badparams=("" "create" "destroy" "snapshot" "rollback" "clone" \
    "promote" "rename" "list -*" "set" "get -*" "inherit" "mount -A" \
    "unmount" "share" "unshare" "send" "receive")

if is_linux; then
	ulimit -c unlimited
	echo "$corefile" >/proc/sys/kernel/core_pattern
	echo 0 >/proc/sys/kernel/core_uses_pid
	export ASAN_OPTIONS="abort_on_error=1:disable_coredump=0"
elif is_freebsd; then
	ulimit -c unlimited
	savedcorefile=$(sysctl -n kern.corefile)
	log_must sysctl kern.corefile=$corepath/core.%N
else
	log_must coreadm -p ${corepath}/core.%f
fi

log_must export ZFS_ABORT=yes

for subcmd in "${cmds[@]}" "${badparams[@]}"; do
	zfs $subcmd >/dev/null 2>&1 && log_fail "$subcmd passed incorrectly."
	if [[ ! -e $corefile ]]; then
		log_fail "zfs $subcmd cannot generate core file with " \
		    "ZFS_ABORT set."
	fi
	log_must rm -f $corefile
done

log_pass "With ZFS_ABORT set, zfs command can abort and generate core file " \
    "as expected."
