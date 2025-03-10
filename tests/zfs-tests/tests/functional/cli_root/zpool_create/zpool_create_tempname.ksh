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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright 2018, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 'zpool create -t <tempname>' can create a pool with the specified temporary
# name. The pool should be present in the namespace as <tempname> until exported
#
# STRATEGY:
# 1. Create a pool with '-t' option
# 2. Verify the pool is created with the specified temporary name
#

verify_runnable "global"

function cleanup
{
	typeset pool

	for pool in $TESTPOOL $TEMPPOOL; do
		poolexists $pool && destroy_pool $pool
	done
}

log_assert "'zpool create -t <tempname>' can create a pool with the specified" \
	" temporary name."
log_onexit cleanup

TEMPPOOL="tempname.$$"
typeset poolprops=('comment=text' 'ashift=12' 'listsnapshots=on' 'autoexpand=on'
    'autoreplace=on' 'delegation=off' 'failmode=continue')
typeset fsprops=('canmount=off' 'mountpoint=none' 'utf8only=on'
    'casesensitivity=mixed' 'version=1' 'normalization=formKD')

for poolprop in "${poolprops[@]}"; do
	for fsprop in "${fsprops[@]}"; do
		# 1. Create a pool with '-t' option
		log_must zpool create -t $TEMPPOOL -O $fsprop -o $poolprop \
			$TESTPOOL $DISKS
		# 2. Verify the pool is created with the specified temporary name
		log_must poolexists $TEMPPOOL
		log_mustnot poolexists $TESTPOOL
		IFS='=' read -r propname propval <<<"$fsprop"
		log_must test "$(get_prop $propname $TEMPPOOL)" == "$propval"
		IFS='=' read -r propname propval <<<"$poolprop"
		log_must test "$(get_pool_prop $propname $TEMPPOOL)" == "$propval"
		# Cleanup
		destroy_pool $TEMPPOOL
	done
done

log_pass "'zpool create -t <tempname>' successfully creates pools with" \
	" temporary names"
