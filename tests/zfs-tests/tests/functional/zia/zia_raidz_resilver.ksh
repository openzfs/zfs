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
