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
#	A PANIC is triggered in dbuf_redirty() if we clone a file, mmap it
#	and write from the map into the file. PR#15656 fixes this scenario.
#	This scenario also causes data corruption on FreeBSD, which is fixed
#	by PR#15665.
#
# STRATEGY:
#	1. Create a pool
#	2. Create a test file 
#	3. Clone, mmap and write to the file using clone_mmap_write
#	5. Synchronize cached writes
#	6. Verfiy data is correctly written to the disk
#

verify_runnable "global"

if is_linux && [[ $(linux_version) -lt $(linux_version "4.5") ]]; then
  log_unsupported "copy_file_range not available before Linux 4.5"
fi

VDIR=$TEST_BASE_DIR/disk-bclone
VDEV="$VDIR/a"

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -rf $VDIR
}

log_onexit cleanup

log_assert "Test for clone, mmap and write scenario"

log_must rm -rf $VDIR
log_must mkdir -p $VDIR
log_must truncate -s 1G $VDEV

log_must zpool create -o feature@block_cloning=enabled $TESTPOOL $VDEV
log_must zfs create $TESTPOOL/$TESTFS

log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS/file bs=1M count=512
log_must clone_mmap_write /$TESTPOOL/$TESTFS/file /$TESTPOOL/$TESTFS/clone

sync_pool $TESTPOOL
log_must sync

log_must have_same_content /$TESTPOOL/$TESTFS/file /$TESTPOOL/$TESTFS/clone
blocks=$(get_same_blocks $TESTPOOL/$TESTFS file $TESTPOOL/$TESTFS clone)
# FreeBSD's seq(1) leaves a trailing space, remove it with sed(1).
log_must [ "$blocks" = "$(seq -s " " 1 4095 | sed 's/ $//')" ]

log_pass "Clone, mmap and write does not cause data corruption or " \
	"trigger panic"
