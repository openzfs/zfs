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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/hole_birth/hole_birth.kshlib

#
# DESCRIPTION:
#	Sleep 5s while editing the file,
#	check if file md5sum matched after send/receive.
#

verify_runnable "both"

log_assert "Verify hole_birth bug of Zol #5030"
log_onexit cleanup_pool $POOL

sendfs=$POOL/sendfs
recvfs=$POOL/recvfs

if datasetexists $recvfs; then
	log_must $ZFS destroy -r $recvfs
fi
if datasetexists $sendfs; then
	log_must $ZFS destroy -r $sendfs
fi

log_must $ZFS create $sendfs -o recordsize=128k

log_must dd if=/dev/urandom of=/$sendfs/file1 bs=4k count=11264 seek=1152
create_snapshot $sendfs snap1

log_must truncate -s 4194304 /$sendfs/file1
log_must dd if=/dev/urandom of=/$sendfs/file1 bs=4k count=152 seek=384 conv=notrunc
sleep 5
log_must dd if=/dev/urandom of=/$sendfs/file1 bs=4k count=10 seek=1408 conv=notrunc
create_snapshot $sendfs snap2

$ZFS send $sendfs@snap1 | $ZFS receive $recvfs
$ZFS send -i $sendfs@snap1 $sendfs@snap2 | $ZFS receive $recvfs

log_must cmp_md5sum /$sendfs/file1 /$recvfs/file1

# Cleanup POOL
log_must cleanup_pool $POOL

log_pass "Verify hole_birth bug of Zol #5030"
