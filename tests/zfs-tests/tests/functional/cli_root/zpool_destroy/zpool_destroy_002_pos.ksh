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
. $STF_SUITE/tests/functional/cli_root/zpool_destroy/zpool_destroy.cfg

#
# DESCRIPTION:
#	'zpool destroy -f <pool>' can forcely destroy the specified pool.
#
# STRATEGY:
#	1. Create a storage pool
#	2. Create some datasets within the pool
#	3. Change directory to any mountpoint of these datasets,
#	   Verify 'zpool destroy' without '-f' will fail.
#	4. 'zpool destroy -f' the pool
#	5. Verify the pool is destroyed successfully
#

verify_runnable "global"

function cleanup
{
	[[ -n $cwd ]] && log_must cd $cwd

	if [[ -d $TESTDIR ]]; then
		ismounted $TESTDIR && log_must umount $TESTDIR
		log_must rm -rf $TESTDIR
	fi

	typeset -i i=0
	while (( $i < ${#datasets[*]} )); do
		datasetexists ${datasets[i]} && \
			destroy_dataset ${datasets[i]}
		(( i = i + 1 ))
	done

	poolexists $TESTPOOL && destroy_pool $TESTPOOL
}

set -A datasets "$TESTPOOL/$TESTFS" "$TESTPOOL/$TESTCTR/$TESTFS1" \
	"$TESTPOOL/$TESTCTR" "$TESTPOOL/$TESTVOL" \

log_assert "'zpool destroy -f <pool>' can forcely destroy the specified pool"

log_onexit cleanup

create_pool $TESTPOOL $DISK0
log_must zfs create $TESTPOOL/$TESTFS
log_must mkdir -p $TESTDIR
log_must zfs set mountpoint=$TESTDIR $TESTPOOL/$TESTFS
log_must zfs create $TESTPOOL/$TESTCTR
log_must zfs create $TESTPOOL/$TESTCTR/$TESTFS1
log_must zfs create -V $VOLSIZE $TESTPOOL/$TESTVOL

typeset -i i=0
while (( $i < ${#datasets[*]} )); do
	datasetexists "${datasets[i]}" || \
		log_fail "Create datasets fail."
	((i = i + 1))
done

log_note "'zpool destroy' without '-f' will fail " \
	"while pool is busy."

for dir in $TESTDIR /$TESTPOOL/$TESTCTR /$TESTPOOL/$TESTCTR/$TESTFS1 ; do
	log_must cd $dir
	log_mustnot zpool destroy $TESTPOOL

	# Need mount here, otherwise some dataset may be unmounted.
	log_must zfs mount -a

	i=0
	while (( i < ${#datasets[*]} )); do
		datasetexists "${datasets[i]}" || \
			log_fail "Dataset ${datasets[i]} removed unexpected."
		((i = i + 1))
	done
done

# 4. 'zpool destroy -f' the pool (unsupported behavior in Linux)
if is_linux; then
	log_must cd $cwd
fi

destroy_pool $TESTPOOL
log_mustnot poolexists "$TESTPOOL"

log_pass "'zpool destroy -f <pool>' success."
