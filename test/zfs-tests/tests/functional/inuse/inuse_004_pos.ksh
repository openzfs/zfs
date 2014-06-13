#!/usr/bin/ksh -p
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
# Copyright (c) 2013 by Delphix. All rights reserved.
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
	# Essentailly this is the default_cleanup rountine but I cannot get it
	# to work correctly.  So its reproduced below.  Still need to full
	# understand why default_cleanup does not work correctly from here.
	#
        log_must $ZFS umount $TESTPOOL/$TESTFS

        $RM -rf $TESTDIR || \
            log_unresolved Could not remove $TESTDIR

	log_must $ZFS destroy $TESTPOOL/$TESTFS
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

	typeset format_file=/var/tmp/format_in.$$.1
	$ECHO "partition" > $format_file
	$ECHO "modify" >> $format_file

	$FORMAT -e -s -d $disk -f $format_file
	typeset -i retval=$?
	$RM -rf $format_file
	return $retval
}

log_assert "format will disallow modification of a mounted zfs disk partition"\
 " or a spare device"

log_onexit cleanup
log_must default_setup_noexit $FS_DISK0
log_must $ZPOOL add $TESTPOOL spare $FS_DISK1

log_note "Attempt to format a ZFS disk"
log_mustnot mini_format $FS_DISK0
log_note "Attempt to format a ZFS spare device"
log_mustnot mini_format $FS_DISK1

log_pass "Unable to format a disk in use by ZFS"
