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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
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

	log_must pop_coredump_pattern "$coresavepath"
	log_must rm -rf $corepath $vdev1 $vdev2 $vdev3

	# Clean up the pool created if we failed to abort.
	poolexists $pool && destroy_pool $pool
}

log_assert "With ZFS_ABORT set, all zpool commands can abort and generate a core file."
log_onexit cleanup

corepath=$TESTDIR/core
corefile=$corepath/core.zpool
coresavepath=$corepath/save
log_must rm -rf $corepath
log_must mkdir $corepath

pool=pool.$$
vdev1=$TESTDIR/file1
vdev2=$TESTDIR/file2
vdev3=$TESTDIR/file3
log_must mkfile $MINVDEVSIZE $vdev1 $vdev2 $vdev3

set -A cmds "create $pool mirror $vdev1 $vdev2" "list $pool" "iostat $pool" \
	"status $pool" "upgrade $pool" "get delegation $pool" "set delegation=off $pool" \
	"export $pool" "import -d $TESTDIR $pool" "offline $pool $vdev1" \
	"online $pool $vdev1" "clear $pool" "detach $pool $vdev2" \
	"attach $pool $vdev1 $vdev2" "replace $pool $vdev2 $vdev3" \
	"scrub $pool" "destroy -f $pool"

set -A badparams "" "create" "destroy" "add" "remove" "list *" "iostat" "status" \
		"online" "offline" "clear" "attach" "detach" "replace" "scrub" \
		"import" "export" "upgrade" "history -?" "get" "set"

log_must eval "push_coredump_pattern \"$corepath\" > \"$coresavepath\""
log_must export ZFS_ABORT=yes

for subcmd in "${cmds[@]}" "${badparams[@]}"; do
	log_mustnot eval "zpool $subcmd"
	log_must rm "$corefile"
done

log_pass "With ZFS_ABORT set, zpool command can abort and generate core file as expected."
