#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2023 Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.cfg

#
# DESCRIPTION:
# 	During a pool import, the 'import_progress' kstat contains details
# 	on the import progress.
#
# STRATEGY:
#	1. Create test pool with several devices
#	2. Generate some ZIL records and spacemap logs
#	3. Export the pool
#	4. Import the pool in the background and monitor the kstat content
#	5. Check the zfs debug messages for import progress
#

verify_runnable "global"

function cleanup
{
	log_must set_tunable64 KEEP_LOG_SPACEMAPS_AT_EXPORT 0
	log_must set_tunable64 METASLAB_DEBUG_LOAD 0

	destroy_pool $TESTPOOL1
}

log_assert "During a pool import, the 'import_progress' kstat contains " \
	"notes on the progress"

log_onexit cleanup

log_must zpool create $TESTPOOL1 $VDEV0 $VDEV1 $VDEV2
typeset guid=$(zpool get -H -o value guid $TESTPOOL1)

log_must zfs create -o recordsize=8k $TESTPOOL1/fs
#
# This dd command works around an issue where ZIL records aren't created
# after freezing the pool unless a ZIL header already exists. Create a file
# synchronously to force ZFS to write one out.
#
log_must dd if=/dev/zero of=/$TESTPOOL1/fs/sync conv=fsync bs=1 count=1

#
# Overwrite some blocks to populate spacemap logs
#
log_must dd if=/dev/urandom of=/$TESTPOOL1/fs/00 bs=1M count=200
sync_all_pools
log_must dd if=/dev/urandom of=/$TESTPOOL1/fs/00 bs=1M count=200
sync_all_pools

#
# Freeze the pool to retain intent log records
#
log_must zpool freeze $TESTPOOL1

# fill_fs [destdir] [dirnum] [filenum] [bytes] [num_writes] [data]
log_must fill_fs /$TESTPOOL1/fs 1 2000 100 1024 R

log_must zpool list -v $TESTPOOL1

#
# Unmount filesystem and export the pool
#
# At this stage the zfs intent log contains
# a set of records to replay.
#
log_must zfs unmount /$TESTPOOL1/fs

log_must set_tunable64 KEEP_LOG_SPACEMAPS_AT_EXPORT 1
log_must zpool export $TESTPOOL1

log_must set_tunable64 METASLAB_DEBUG_LOAD 1
log_note "Starting zpool import in background at" $(date +'%H:%M:%S')
zpool import -d $DEVICE_DIR -f $guid &

#
# capture progress until import is finished
#
kstat import_progress
while [[ -n $(jobs) ]]; do
	line=$(kstat import_progress | grep -v pool_guid)
	if [[ -n $line ]]; then
		echo $line
	fi
	sleep 0.0001
done
log_note "zpool import completed at" $(date +'%H:%M:%S')

entries=$(kstat dbgmsg | grep "spa_import_progress_set_notes_impl(): 'testpool1'" | wc -l)
log_note "found $entries progress notes in dbgmsg"
log_must test $entries -gt 20

log_must zpool status $TESTPOOL1

log_pass "During a pool import, the 'import_progress' kstat contains " \
	"notes on the progress"
