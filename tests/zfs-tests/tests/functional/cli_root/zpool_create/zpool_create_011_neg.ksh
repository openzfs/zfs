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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.shlib

#
# DESCRIPTION:
# 'zpool create' will fail in the following cases:
# existent pool; device is part of an active pool; nested virtual devices;
# differently sized devices without -f option; device being currently
# mounted; devices in /etc/vfstab; specified as the dedicated dump device.
#
# STRATEGY:
# 1. Create case scenarios
# 2. For each scenario, try to create a new pool with the virtual devices
# 3. Verify the creation is failed.
#

verify_runnable "global"

function cleanup
{
	for pool in $TESTPOOL $TESTPOOL1
	do
		destroy_pool $pool
	done

	rm -rf $disk1 $disk2 $disk3 $disk4

	if [[ -n $saved_dump_dev ]]; then
		log_must dumpadm -u -d $saved_dump_dev
	fi
}

log_assert "'zpool create' should be failed with inapplicable scenarios."
log_onexit cleanup

disk1=$(create_blockfile $FILESIZE)
disk2=$(create_blockfile $FILESIZE)
disk3=$(create_blockfile $FILESIZE)
disk4=$(create_blockfile $FILESIZE1)
mirror1="$DISK0 $DISK1"
mirror2="$disk1 $disk2"
raidz1=$mirror1
raidz2=$mirror2
draid1="$DISK0 $DISK1 $DISK2"
draid2="$disk1 $disk2 $disk3"
diff_size_dev="$disk2 $disk4"
draid_diff_size_dev="$disk1 $disk2 $disk4"
vfstab_dev=$(find_vfstab_dev)

if is_illumos; then
	specified_dump_dev=${DISK0}s0
	saved_dump_dev=$(save_dump_dev)

	cyl=$(get_endslice $DISK0 6)
	log_must set_partition 7 "$cyl" $SIZE1 $DISK0
fi
create_pool $TESTPOOL $DISK0

#
# Set up the testing scenarios parameters
#
set -A arg \
	"$TESTPOOL1 $DISK0" \
	"$TESTPOOL1 mirror mirror $mirror1 mirror $mirror2" \
	"$TESTPOOL1 raidz raidz $raidz1 raidz $raidz2" \
	"$TESTPOOL1 raidz1 raidz1 $raidz1 raidz1 $raidz2" \
	"$TESTPOOL1 draid draid $draid draid $draid2" \
	"$TESTPOOL1 mirror raidz $raidz1 raidz $raidz2" \
	"$TESTPOOL1 mirror raidz1 $raidz1 raidz1 $raidz2" \
	"$TESTPOOL1 mirror draid $draid1 draid $draid2" \
	"$TESTPOOL1 raidz mirror $mirror1 mirror $mirror2" \
	"$TESTPOOL1 raidz1 mirror $mirror1 mirror $mirror2" \
	"$TESTPOOL1 draid1 mirror $mirror1 mirror $mirror2" \
	"$TESTPOOL1 mirror $diff_size_dev" \
	"$TESTPOOL1 raidz $diff_size_dev" \
	"$TESTPOOL1 raidz1 $diff_size_dev" \
	"$TESTPOOL1 draid1 $draid_diff_size_dev" \
	"$TESTPOOL1 mirror $mirror1 spare $mirror2 spare $diff_size_dev" \
	"$TESTPOOL1 $vfstab_dev" \
	"$TESTPOOL1 ${DISK0}s10" \
	"$TESTPOOL1 spare $pooldev2"

unset NOINUSE_CHECK
typeset -i i=0
while (( i < ${#arg[*]} )); do
	log_mustnot zpool create ${arg[i]}
	(( i = i+1 ))
done

# now destroy the pool to be polite
log_must zpool destroy -f $TESTPOOL

if is_illumos; then
	# create/destroy a pool as a simple way to set the partitioning
	# back to something normal so we can use this $disk as a dump device
	log_must zpool create -f $TESTPOOL3 $DISK1
	log_must zpool destroy -f $TESTPOOL3

	log_must dumpadm -d ${DEV_DSKDIR}/$specified_dump_dev
	log_mustnot zpool create -f $TESTPOOL1 "$specified_dump_dev"

	# Also check to see that in-use checking prevents us from creating
	# a zpool from just the first slice on the disk.
	log_mustnot zpool create \
		-f $TESTPOOL1 ${specified_dump_dev}s0
fi

log_pass "'zpool create' is failed as expected with inapplicable scenarios."
