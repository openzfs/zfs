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
# Copyright (c) 2022 George Amanakis. All rights reserved.
#

#
# DESCRIPTION:
# Verify correct output with 'zpool status -v' after corrupting a file
#
# STRATEGY:
# 1. Create a pool, an encrypted filesystem and a file
# 2. zinject checksum errors
# 3. Unmount the filesystem and unload the key
# 4. Scrub the pool
# 5. Verify we report that errors were detected but we do not report
#	the filename since the key is not loaded.
# 6. Load the key and mount the encrypted fs.
# 7. Verify we report errors in the pool in 'zpool status -v'

. $STF_SUITE/include/libtest.shlib

verify_runnable "both"

DISK=${DISKS%% *}

function cleanup
{
	log_must zinject -c all
	destroy_pool $TESTPOOL2
	rm -f $TESTDIR/vdev_a
}

log_assert "Verify reporting errors with unloaded keys works"
log_onexit cleanup

typeset passphrase="password"
typeset file="/$TESTPOOL2/$TESTFS1/$TESTFILE0"

truncate -s $MINVDEVSIZE $TESTDIR/vdev_a
log_must zpool create -f -o feature@head_errlog=enabled $TESTPOOL2 $TESTDIR/vdev_a

log_must eval "echo $passphrase > /$TESTPOOL2/pwd"

log_must zfs create -o encryption=aes-256-ccm -o keyformat=passphrase \
    -o keylocation=file:///$TESTPOOL2/pwd -o primarycache=none \
    $TESTPOOL2/$TESTFS1

log_must dd if=/dev/urandom of=$file bs=1024 count=1024 oflag=sync
log_must eval "echo 'aaaaaaaa' >> "$file

corrupt_blocks_at_level $file 0
log_must zfs umount $TESTPOOL2/$TESTFS1
log_must zfs unload-key -a
log_must zpool sync $TESTPOOL2
log_must zpool scrub $TESTPOOL2
log_must zpool wait -t scrub $TESTPOOL2
log_must zpool status -v $TESTPOOL2
log_mustnot eval "zpool status -v $TESTPOOL2 | \
    grep \"permission denied\""
log_mustnot eval "zpool status -v $TESTPOOL2 | grep '$file'"

log_must eval "cat /$TESTPOOL2/pwd | zfs load-key $TESTPOOL2/$TESTFS1"
log_must zfs mount $TESTPOOL2/$TESTFS1
log_must zpool status -v $TESTPOOL2
log_must eval "zpool status -v $TESTPOOL2 | \
    grep \"Permanent errors have been detected\""
log_must eval "zpool status -v $TESTPOOL2 | grep '$file'"

log_pass "Verify reporting errors with unloaded keys works"
