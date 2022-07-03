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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.shlib

#
# DESCRIPTION:
# 'zpool create -f <pool> <vspec> ...' can successfully create a
# new pool in some cases.
#
# STRATEGY:
# 1. Prepare the scenarios for '-f' option
# 2. Use -f to override the devices to create new pools
# 3. Verify the pool created successfully
#

verify_runnable "global"

function cleanup
{
	for pool in $TESTPOOL $TESTPOOL1; do
		poolexists $pool && destroy_pool $pool
	done

	rm -f $disk1 $disk2
	if is_freebsd; then
		umount -f $TESTDIR
		rm -rf $TESTDIR
	fi
}

log_onexit cleanup

log_assert "'zpool create -f <pool> <vspec> ...' can successfully create" \
	"a new pool in some cases."

create_pool $TESTPOOL $DISK0
log_must eval "new_fs ${DEV_RDSKDIR}/${DISK1} >/dev/null 2>&1"
typeset disk1=$(create_blockfile $FILESIZE)
typeset disk2=$(create_blockfile $FILESIZE1)

unset NOINUSE_CHECK
log_must zpool export $TESTPOOL
log_note "'zpool create' without '-f' will fail " \
	"while device belongs to an exported pool."
log_mustnot zpool create $TESTPOOL1 $DISK0
create_pool $TESTPOOL1 $DISK0
log_must poolexists $TESTPOOL1

log_must destroy_pool $TESTPOOL1

log_note "'zpool create' without '-f' will fail " \
	"while device is in use by a ufs filesystem."
if is_freebsd; then
	# fs must be mounted for create to fail on FreeBSD
	log_must mkdir -p $TESTDIR
	log_must mount ${DEV_DSKDIR}/${DISK1} $TESTDIR
fi
log_mustnot zpool create $TESTPOOL $DISK1
if is_freebsd; then
	# fs must not be mounted to create pool even with -f
	log_must umount -f $TESTDIR
	log_must rm -rf $TESTDIR
fi
create_pool $TESTPOOL $DISK1
log_must poolexists $TESTPOOL

log_must destroy_pool $TESTPOOL

log_note "'zpool create' mirror without '-f' will fail " \
	"while devices have different size."
log_mustnot zpool create $TESTPOOL mirror $disk1 $disk2
create_pool $TESTPOOL mirror $disk1 $disk2
log_must poolexists $TESTPOOL

log_must destroy_pool $TESTPOOL

if ! is_freebsd; then
	log_note "'zpool create' mirror without '-f' will fail " \
		"while devices are of different types."
	log_mustnot zpool create $TESTPOOL mirror $disk1 $DISK0
	create_pool $TESTPOOL mirror $disk1 $DISK0
	log_must poolexists $TESTPOOL

	log_must destroy_pool $TESTPOOL
fi

log_note "'zpool create' without '-f' will fail " \
	"while a device is part of a potentially active pool."
create_pool $TESTPOOL mirror $DISK0 $DISK1
log_must zpool offline $TESTPOOL $DISK0
log_must zpool export $TESTPOOL
log_mustnot zpool create $TESTPOOL1 $DISK0
create_pool $TESTPOOL1 $DISK0
log_must poolexists $TESTPOOL1

log_must destroy_pool $TESTPOOL1

log_pass "'zpool create -f <pool> <vspec> ...' success."
