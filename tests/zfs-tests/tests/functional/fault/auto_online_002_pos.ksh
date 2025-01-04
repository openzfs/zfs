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
# Copyright (c) 2016, 2017 by Intel Corporation. All rights reserved.
# Copyright (c) 2019 by Delphix. All rights reserved.
# Portions Copyright 2021 iXsystems, Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/fault/fault.cfg

#
# DESCRIPTION:
# Testing Fault Management Agent ZED Logic - Automated Auto-Online Test.
# Now with partitioned vdevs.
#
# STRATEGY:
# 1. Partition a scsi_debug device for simulating removal
# 2. Create a pool
# 3. Offline disk
# 4. ZED polls for an event change for online disk to be automatically
#    added back to the pool.
#
verify_runnable "both"

function cleanup
{
	poolexists ${TESTPOOL} && destroy_pool ${TESTPOOL}
	unload_scsi_debug
}

log_assert "Testing automated auto-online FMA test with partitioned vdev"

log_onexit cleanup

load_scsi_debug ${SDSIZE} ${SDHOSTS} ${SDTGTS} ${SDLUNS} '512b'
SDDEVICE=$(get_debug_device)
zpool labelclear -f ${SDDEVICE}
partition_disk ${SDSIZE} ${SDDEVICE} 1
part=${SDDEVICE}1
host=$(get_scsi_host ${SDDEVICE})

block_device_wait /dev/${part}
log_must zpool create -f ${TESTPOOL} raidz1 ${part} ${DISKS}

# Add some data to the pool
log_must mkfile ${FSIZE} /${TESTPOOL}/data

remove_disk ${SDDEVICE}
check_state ${TESTPOOL} "" "degraded" || \
    log_fail "${TESTPOOL} is not degraded"

# Clear zpool events
log_must zpool events -c

# Online disk
insert_disk ${SDDEVICE} ${host}

log_note "Delay for ZED auto-online"
typeset -i timeout=0
until is_pool_resilvered ${TESTPOOL}; do
	if ((timeout++ == MAXTIMEOUT)); then
		log_fail "Timeout occurred"
	fi
	sleep 1
done
log_note "Auto-online of ${SDDEVICE} is complete"

# Validate auto-online was successful
sleep 1
check_state ${TESTPOOL} "" "online" || \
    log_fail "${TESTPOOL} is not back online"

log_must zpool destroy ${TESTPOOL}

log_pass "Auto-online with partitioned vdev test successful"
