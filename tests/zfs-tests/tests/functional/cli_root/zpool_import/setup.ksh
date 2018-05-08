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
. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.cfg

verify_runnable "global"
verify_disk_count "$DISKS" 2

if ! $(is_physical_device $ZFS_DISK1) ; then
	log_unsupported "Only partitionable physical disks can be used"
fi

DISK=${DISKS%% *}

for dev in $ZFS_DISK1 $ZFS_DISK2 ; do
	log_must cleanup_devices $dev
done

typeset -i i=0
while (( i <= $GROUP_NUM )); do
	if ! is_linux; then
		if (( i == 2 )); then
			(( i = i + 1 ))
			continue
		fi
	fi
	log_must set_partition $i "$cyl" $SLICE_SIZE $ZFS_DISK1
	cyl=$(get_endslice $ZFS_DISK1 $i)
	(( i = i + 1 ))
done

create_pool "$TESTPOOL" "$ZFSSIDE_DISK1"

if [[ -d $TESTDIR ]]; then
	rm -rf $TESTDIR  || log_unresolved Could not remove $TESTDIR
	mkdir -p $TESTDIR || log_unresolved Could not create $TESTDIR
fi

log_must zfs create $TESTPOOL/$TESTFS
log_must zfs set mountpoint=$TESTDIR $TESTPOOL/$TESTFS

[[ ! -d $DEVICE_DIR ]] && \
	log_must mkdir -p $DEVICE_DIR

i=0
while (( i < $MAX_NUM )); do
	log_must mkfile $FILE_SIZE ${DEVICE_DIR}/${DEVICE_FILE}$i
	(( i = i + 1 ))
done

log_pass
