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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/block_cloning/block_cloning.kshlib

#
# DESCRIPTION:
#	Verify that cloning a file at a large offset is possible.
#
# STRATEGY:
#   1. Create dataset.
#   2. Populate the source file with 1024 blocks at 1024 block offset.
#   3. Clone 1024 blocks at a 1024-block offset.
#   4. Compare the cloned file with the original file.
#

verify_runnable "global"

if is_linux && [[ $(linux_version) -lt $(linux_version "4.5") ]]; then
  log_unsupported "copy_file_range not available before Linux 4.5"
fi

claim="The first clone at a large offset is functional"

log_assert $claim

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_onexit cleanup

#
# 1. Create dataset.
#
log_must zpool create -o feature@block_cloning=enabled $TESTPOOL $DISKS
sync_pool $TESTPOOL

#
# 2. Populate the source file with 1024 blocks at 1024 block offset.
#
log_must dd if=/dev/urandom of=/$TESTPOOL/file1 \
    oflag=sync bs=128k count=1024 seek=1024
sync_pool $TESTPOOL

#
# 3. Clone 1024 blocks at a 1024-block offset.
#
log_must clonefile -f /$TESTPOOL/file1 /$TESTPOOL/file2 134217728 134217728 \
    134217728
sync_pool $TESTPOOL

#
# 4. Compare the cloned file with the original file.
#
log_must have_same_content /$TESTPOOL/file1 /$TESTPOOL/file2
typeset blocks=$(get_same_blocks $TESTPOOL file1 $TESTPOOL file2)

# FreeBSD's seq(1) leaves a trailing space, remove it with sed(1).
log_must [ "$blocks" = "$(seq -s " " 0 1023 | sed 's/ $//')" ]

log_pass $claim
