#!/usr/bin/ksh
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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.shlib

#
# DESCRIPTION:
# 'zpool create [-f]' can create a storage pool with large number of
# file-in-zfs-filesystem-based vdevs without any errors.
#
# STRATEGY:
# 1. Create assigned number of files in ZFS filesystem as vdevs
# 2. Creating a new pool based on the vdevs should get success
# 3. Fill in the filesystem and create a partially writen file as vdev
# 4. Add the new file into vdevs list and create a pool
# 5. Creating a storage pool with the new vdevs list should be failed.
#

verify_runnable "global"

function cleanup
{
	typeset pool=""

	for pool in foo $TESTPOOL2 $TESTPOOL1; do
		poolexists $pool && \
			destroy_pool $pool
	done

	if datasetexists $TESTPOOL/$TESTFS; then
		log_must $ZFS destroy -f $TESTPOOL/$TESTFS
	fi

	if poolexists $TESTPOOL; then
		destroy_pool $TESTPOOL
	fi

	if [[ -d $TESTDIR ]]; then
		log_must $RM -rf $TESTDIR
	fi

	partition_disk $SIZE $disk 6
}

#
# Create a pool and fs on the assigned disk, and dynamically create large
# numbers of files as vdevs.(the default value is <VDEVS_NUM>)
#

function setup_vdevs #<disk>
{
	typeset disk=$1
	typeset -l largest_num=0
	typeset -l slice_size=0
	typeset vdev=""


	#
	# Get disk size for zfs filesystem
	#
	create_pool foo $disk
	log_must $ZFS create foo/fs
	typeset -l fs_size=$(get_prop "available" foo/fs)
	destroy_pool foo

	(( largest_num = fs_size / (1024 * 1024 * ${POOL_MINSIZE}) ))
	if (( largest_num < $VDEVS_NUM )); then
		#minus $largest_num/$MD_OVERHEAD to leave space for metadata
		(( vdevs_num=largest_num - largest_num/$MD_OVERHEAD ))
		file_size=$POOL_MINSIZE
		vdev=$disk
	else
		vdevs_num=$VDEVS_NUM
		(( file_size = fs_size / \
		 (1024 * 1024 * (vdevs_num + vdevs_num/$MD_OVERHEAD)) ))
		if (( file_size > FILE_SIZE )); then
			# If file_size too large, the time cost will increase so
                        # we limit the file_size to $FILE_SIZE, and thus the
			# total time spent on creating file can be controlled
			# within the timeout.
			file_size=$FILE_SIZE
		fi
		# Add $vdevs_num/$MD_OVERHEAD to provide enough space for
		# metadata
		(( slice_size = file_size * (vdevs_num + \
		    vdevs_num/$MD_OVERHEAD) ))
		set_partition 0 "" ${slice_size}m $disk
		vdev=${disk}s0
        fi

	create_pool $TESTPOOL $vdev
	[[ -d $TESTDIR ]] && \
		log_must $RM -rf $TESTDIR
        log_must $MKDIR -p $TESTDIR
        log_must $ZFS create $TESTPOOL/$TESTFS
        log_must $ZFS set mountpoint=$TESTDIR $TESTPOOL/$TESTFS

	typeset -l count=0
	typeset PIDLIST=""
	while (( count < vdevs_num )); do
		$MKFILE -s ${file_size}m ${TESTDIR}/file.$count &
		PIDLIST="$PIDLIST $!"
		vdevs_list="$vdevs_list ${TESTDIR}/file.$count"
		(( count = count + 1 ))
	done

	# wait all mkfiles to finish
        wait $PIDLIST
        if (( $? != 0 )); then
                log_fail "create vdevs failed."
        fi

        return 0
}

log_assert " 'zpool create [-f]' can create a storage pool with large " \
    "numbers of vdevs without any errors."
log_onexit cleanup

if [[ -n $DISK ]]; then
        disk=$DISK
else
        disk=$DISK0
fi

log_note "Create storage pool with number of $VDEVS_NUM file vdevs should " \
    "succeed."
vdevs_list=""
vdevs_num=$VDEVS_NUM
file_size=$FILE_SIZE

setup_vdevs $disk
create_pool $TESTPOOL1 $vdevs_list

if poolexists $TESTPOOL1; then
	destroy_pool $TESTPOOL1
else
	log_fail " Creating pool with large numbers of file-vdevs fail."
fi

log_note "Creating storage pool with partially written file as vdev should " \
    "fail."

left_space=$(get_prop "available" $TESTPOOL/$TESTFS)
# Count the broken file size. make sure it should be greater than $left_space
# so, here, we plus a number -- $file_size, this number can be any other number.
(( file_size = left_space / (1024 * 1024) + file_size ))
log_mustnot $MKFILE -s ${file_size}m ${TESTDIR}/broken_file
vdevs_list="$vdevs_list ${TESTDIR}/broken_file"

log_mustnot $ZPOOL create -f $TESTPOOL2 $vdevs_list

log_pass "'zpool create [-f]' with $vdevs_num vdevs success."
