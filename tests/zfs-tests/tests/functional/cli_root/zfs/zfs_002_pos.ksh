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

	log_must pop_coredump_pattern "$coresavepath"

	for ds in $fs1 $fs $ctr; do
		datasetexists $ds && destroy_dataset $ds -rRf
	done
}

log_assert "With ZFS_ABORT set, all zfs commands can abort and generate a core file."
log_onexit cleanup

ctr=$TESTPOOL/$TESTCTR
log_must zfs create -p $ctr

# Preparation work for testing
corepath=/$ctr
corefile=$corepath/core.zfs
coresavepath=$corepath/save

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

log_must eval "push_coredump_pattern \"$corepath\" > \"$coresavepath\""
log_must export ZFS_ABORT=yes

for subcmd in "${cmds[@]}" "${badparams[@]}"; do
	log_mustnot eval "zfs $subcmd"
	log_must rm "$corefile"
done

log_pass "With ZFS_ABORT set, zfs command can abort and generate core file as expected."
