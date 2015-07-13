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
. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# With ZFS_ABORT set, all zpool commands should be able to abort and generate a core file.
#
# STRATEGY:
# 1. Create an array of zpool command
# 2. Execute each command in the array
# 3. Verify the command aborts and generate a core file
#

verify_runnable "global"

function cleanup
{
	unset ZFS_ABORT

	if [[ -d $corepath ]]; then
		$RM -rf $corepath
	fi
	destroy_pool -f $pool
	if [[ -n "$LINUX" && -n "$def_cor_pat" ]]; then
		echo "$def_cor_pat" > /proc/sys/kernel/core_pattern
		echo "$def_cor_suid" > /proc/sys/fs/suid_dumpable
		ulimit -c $ulimit
	fi
}

log_assert "With ZFS_ABORT set, all zpool commands can abort and generate a core file."
log_onexit cleanup

#preparation work for testing
corepath=$TEST_BASE_DIR/core
if [[ -d $corepath ]]; then
	$RM -rf $corepath
fi
$MKDIR -p $corepath

pool=pool.$$
vdev1=$TESTDIR/file1
vdev2=$TESTDIR/file2
vdev3=$TESTDIR/file3
for vdev in $vdev1 $vdev2 $vdev3; do
	log_must $MKFILE -s 64m $vdev
done

set -A cmds "create $pool mirror $vdev1 $vdev2" "list $pool" "iostat $pool" \
	"status $pool" "upgrade $pool" "get delegation $pool" "set delegation=off $pool" \
	"export $pool" "import -d $TESTDIR $pool" "offline $pool $vdev1" \
	"online $pool $vdev1" "clear $pool" "detach $pool $vdev2" \
	"attach $pool $vdev1 $vdev2" "replace $pool $vdev2 $vdev3" \
	"scrub $pool" "destroy -f $pool"

set -A badparams "" "create" "destroy" "add" "remove" "list *" "iostat" "status" \
		"online" "offline" "clear" "attach" "detach" "replace" "scrub" \
		"import" "export" "upgrade" "history -?" "get" "set"

if [[ -n "$LINUX" && -f "/proc/sys/kernel/core_pattern" ]]; then
	# NOTE: This file is only provided if the kernel was built
	#       with the CONFIG_ELF_CORE configuration option
	typeset def_cor_pat=$(cat /proc/sys/kernel/core_pattern)
	typeset def_cor_suid=$(cat /proc/sys/fs/suid_dumpable)
	typeset ulimit=$(ulimit -c)
	echo "${corepath}/core.%e" > /proc/sys/kernel/core_pattern
	ulimit -c unlimited
else
	log_must $COREADM -p ${corepath}/core.%f
fi
export ZFS_ABORT=yes

for subcmd in "${cmds[@]}" "${badparams[@]}"; do
	$ZPOOL $subcmd >/dev/null 2>&1
	corefile=${corepath}/core.zpool
	if [[ ! -e $corefile ]]; then
		log_fail "$ZPOOL $subcmd cannot generate core file  with ZFS_ABORT set."
	fi
	$RM -f $corefile
done

log_pass "With ZFS_ABORT set, zpool command can abort and generate core file as expected."
