#!/bin/ksh -p

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
# Copyright (c) 2015 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib
. $STF_SUITE/include/properties.shlib

#
# Description:
# Verify compressed send streams can still preserve properties
#
# Strategy:
# 1. Randomly modify the properties in the src pool
# 2. Send a full compressed stream with -p to preserve properties
# 3. Verify all the received properties match the source datasets
# 4. Repeat the process with -R instead of -p
#

verify_runnable "global"

function cleanup
{
	destroy_pool $POOL
	destroy_pool $POOL2
	log_must zpool create $POOL $DISK1
	log_must zpool create $POOL2 $DISK2
	log_must setup_test_model $POOL
}

log_assert "Compressed send doesn't interfere with preservation of properties"
log_onexit cleanup

typeset -a datasets=("" "/pclone" "/$FS" "/$FS/fs1" "/$FS/fs1/fs2"
    "/$FS/fs1/fclone" "/vol" "/$FS/vol")

typeset ds
for opt in "-p" "-R"; do
	for ds in ${datasets[@]}; do
		randomize_ds_props $POOL$ds
	done

	log_must eval "zfs send -c $opt $POOL@final > $BACKDIR/pool-final$opt"
	log_must eval "zfs receive -d -F $POOL2 < $BACKDIR/pool-final$opt"

	for ds in ${datasets[@]}; do
		log_must cmp_ds_prop $POOL$ds $POOL2$ds
		log_must cmp_ds_prop $POOL$ds@final $POOL2$ds@final
	done

	# Don't cleanup the second time, since we do that on exit anyway.
	[[ $opt = "-p" ]] && cleanup
done

log_pass "Compressed send doesn't interfere with preservation of properties"
