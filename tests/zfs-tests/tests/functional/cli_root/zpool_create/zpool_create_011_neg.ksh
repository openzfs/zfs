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
# Copyright (c) 2012 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.shlib
. $TMPFILE

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
                destroy_pool -f $pool
        done

	if [[ -n $saved_dump_dev ]]; then
		log_must $DUMPADM -u -d $saved_dump_dev
	fi

	[[ -n "$LINUX" ]] && disk=$DISK0_orig
        partition_disk $SIZE $disk 6
	[[ -n "$LINUX" ]] && update_lo_mappings $disk
}

log_assert "'zpool create' should be failed with inapplicable scenarios."
log_onexit cleanup

if [[ -n $DISK ]]; then
        disk=$DISK
else
        disk=$DISK0
fi

typeset slice_part=s
[[ -n "$LINUX" ]] && slice_part=p

pooldev1=${disk}${slice_part}${SLICE0}
pooldev2=${disk}${slice_part}${SLICE1}
mirror1="${disk}${slice_part}${SLICE1} ${disk}${slice_part}${SLICE3}"
mirror2="${disk}${slice_part}${SLICE4} ${disk}${slice_part}${SLICE5}"
raidz1=$mirror1
raidz2=$mirror2
diff_size_dev="${disk}${slice_part}${SLICE6} ${disk}${slice_part}${SLICE7}"
vfstab_dev=$(find_vfstab_dev)
specified_dump_dev=${disk}${slice_part}${SLICE0}
saved_dump_dev=$(save_dump_dev)

cyl=$(get_endslice $disk $SLICE6)
set_partition $SLICE7 "$cyl" $SIZE1 $disk
create_pool "$TESTPOOL" "$pooldev1"

#
# Set up the testing scenarios parameters
#
set -A arg "$TESTPOOL $pooldev2" \
        "$TESTPOOL1 $pooldev1" \
        "$TESTPOOL1 $TESTDIR0/$FILEDISK0" \
        "$TESTPOOL1 mirror mirror $mirror1 mirror $mirror2" \
        "$TESTPOOL1 raidz raidz $raidz1 raidz $raidz2" \
        "$TESTPOOL1 raidz1 raidz1 $raidz1 raidz1 $raidz2" \
        "$TESTPOOL1 mirror raidz $raidz1 raidz $raidz2" \
        "$TESTPOOL1 mirror raidz1 $raidz1 raidz1 $raidz2" \
        "$TESTPOOL1 raidz mirror $mirror1 mirror $mirror2" \
        "$TESTPOOL1 raidz1 mirror $mirror1 mirror $mirror2" \
        "$TESTPOOL1 mirror $diff_size_dev" \
        "$TESTPOOL1 raidz $diff_size_dev" \
        "$TESTPOOL1 raidz1 $diff_size_dev" \
	"$TESTPOOL1 mirror $mirror1 spare $mirror2 spare $diff_size_dev" \
        "$TESTPOOL1 $vfstab_dev" \
        "$TESTPOOL1 ${disk}s10" \
	"$TESTPOOL1 spare $pooldev2"

typeset -i i=0
while (( i < ${#arg[*]} )); do
        log_mustnot $ZPOOL create ${arg[i]}
        (( i = i+1 ))
done

# now destroy the pool to be polite
destroy_pool -f $TESTPOOL

# create/destroy a pool as a simple way to set the partitioning
# back to something normal so we can use this $disk as a dump device
log_must $ZPOOL create -f $TESTPOOL3 $disk
destroy_pool -f $TESTPOOL3

log_must $DUMPADM -d $DEV_DSKDIR/$specified_dump_dev
log_mustnot $ZPOOL create -f $TESTPOOL1 "$specified_dump_dev"

# Also check to see that in-use checking prevents us from creating
# a zpool from just the first slice on the disk.
log_mustnot $ZPOOL create -f $TESTPOOL1 ${specified_dump_dev}s0

log_pass "'zpool create' is failed as expected with inapplicable scenarios."
