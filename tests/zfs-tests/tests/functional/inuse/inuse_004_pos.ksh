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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/inuse/inuse.cfg

#
# DESCRIPTION:
# format will disallow modification of a mounted zfs disk partition or a spare
# device
#
# STRATEGY:
# 1. Create a ZFS filesystem
# 2. Add a spare device to the ZFS pool
# 3. Attempt to format the disk and the spare device.
#

verify_runnable "global"

function cleanup
{
	#
	# Essentially this is the default_cleanup routine but I cannot get it
	# to work correctly.  So its reproduced below.  Still need to fully
	# understand why default_cleanup does not work correctly from here.
	#
    log_must zfs umount $TESTPOOL/$TESTFS

    rm -rf $TESTDIR ||
        log_unresolved Could not remove $TESTDIR

	log_must zfs destroy $TESTPOOL/$TESTFS
	destroy_pool $TESTPOOL
}
#
# Currently, if a ZFS disk gets formatted things go horribly wrong, hence the
# mini_format function.  If the modify option is reached, then we know format
# would happily continue - best to not go further.
#
function mini_format
{
        typeset disk=$1

	if is_linux; then
		parted $disk -s -- mklabel gpt
	elif is_freebsd; then
		gpart create -s gpt $disk
	else
		format -e -s -d $disk -f <(printf '%s\n' partition modify)
	fi
}

log_assert "format will disallow modification of a mounted zfs disk partition"\
 " or a spare device"

log_onexit cleanup
log_must default_setup_noexit $FS_DISK0
log_must zpool add $TESTPOOL spare $FS_DISK1

log_note "Attempt to format a ZFS disk"
log_mustnot mini_format $FS_DISK0
log_note "Attempt to format a ZFS spare device"
log_mustnot mini_format $FS_DISK1

log_pass "Unable to format a disk in use by ZFS"
