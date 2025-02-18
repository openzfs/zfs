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
#	When the destination file is mmaped and is already cached we need to
#	update mmaped pages after successful clone.
#
# STRATEGY:
#	1. Create a pool.
#	2. Create a two test files with random content.
#	3. mmap the files, read them and clone from one to the other using
#	   clone_mmap_cached.
#	4. clone_mmap_cached also verifies if the content of the destination
#	   file was updated while reading it from mmaped memory.
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

log_assert "Test for clone into mmaped and cached file"

log_must rm -rf $VDIR
log_must mkdir -p $VDIR
log_must truncate -s 1G $VDEV

log_must zpool create -o feature@block_cloning=enabled $TESTPOOL $VDEV
log_must zfs create $TESTPOOL/$TESTFS

for opts in "--" "-i" "-o" "-io"
do
	log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS/src bs=1M count=1
	log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS/dst bs=1M count=1

	# Clear cache.
	log_must zpool export $TESTPOOL
	log_must zpool import -d $VDIR $TESTPOOL

	log_must clone_mmap_cached $opts /$TESTPOOL/$TESTFS/src /$TESTPOOL/$TESTFS/dst

	sync_pool $TESTPOOL
	log_must sync

	log_must have_same_content /$TESTPOOL/$TESTFS/src /$TESTPOOL/$TESTFS/dst
	blocks=$(get_same_blocks $TESTPOOL/$TESTFS src $TESTPOOL/$TESTFS dst)
	# FreeBSD's seq(1) leaves a trailing space, remove it with sed(1).
	log_must [ "$blocks" = "$(seq -s " " 0 7 | sed 's/ $//')" ]
done

log_pass "Clone properly updates mmapped and cached pages"
