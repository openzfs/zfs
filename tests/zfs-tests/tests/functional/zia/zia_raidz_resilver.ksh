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
# Copyright (c) 2021 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/zia/zia.kshlib

#
# DESCRIPTION:
#	Z.I.A. RAIDZ Resilver works
#
# STRATEGY:
#   1. Turn on all offloads
#   2. Write data to the zpool
#   3. Replace a drive
#   4. Resilver the zpool with Z.I.A.
#   5. Check for errors
#

log_must truncate -s 4G "${RESILVER_REPLACEMENT}"

function cleanup
{
    log_must rm "${RESILVER_REPLACEMENT}"
}
log_onexit cleanup

log_must offload_all

# write a file
log_must file_write -o create -f "${FILENAME}" -b "${BLOCKSZ}" -c "${NUM_WRITES}" -d "${DATA}"
log_must ls -l "${FILENAME}"

# pick a random backing device to offline and replace it
bad="$(random_disk ${DISKS})"
log_must zpool offline "${TESTPOOL}" "${bad}"
log_must zpool replace "${TESTPOOL}" "${bad}" "${RESILVER_REPLACEMENT}"
log_must wait_replacing "${TESTPOOL}"

log_must verify_pool "${TESTPOOL}"
log_must check_pool_status "${TESTPOOL}" "errors" "No known data errors"

log_pass
