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
# Copyright 2016, loli10K. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that 'zfs unshare [nfs|smb] -a' unshares only filesystems shared by the
# specified protocol.
#
# STRATEGY:
# 1. Share filesystems with different protocols.
# 2. Invoke 'zfs unshare nfs -a' to unshare filesystems.
# 3. Verify that only nfs filesystems are unshared.
# 4. Share all filesystems again.
# 5. Invoke 'zfs unshare smb -a' and verify only smb filesystems are unshared.
#

verify_runnable "global"

[ -d "/var/lib/samba/usershares" ] || log_unsupported "Samba usershares disabled"

function cleanup
{
	log_must zfs unshare -a
	log_must zfs destroy -f $TESTPOOL/$TESTFS/shared1
	log_must zfs destroy -f $TESTPOOL/$TESTFS/shared2
	log_must zfs destroy -f $TESTPOOL/$TESTFS/shared3
	log_must rm -f /var/lib/samba/usershares/testpool_testfs_shared{2,3}
}

log_assert "Verify 'zfs unshare [nfs|smb] -a' only works on the specified" \
	"protocol."
log_onexit cleanup

# 1. Share filesystems with different protocols.
log_must zfs create $TESTPOOL/$TESTFS/shared1
log_must zfs create $TESTPOOL/$TESTFS/shared2
log_must zfs create $TESTPOOL/$TESTFS/shared3
log_must zfs set mountpoint=$TESTDIR/1 $TESTPOOL/$TESTFS/shared1
log_must zfs set mountpoint=$TESTDIR/2 $TESTPOOL/$TESTFS/shared2
log_must zfs set mountpoint=$TESTDIR/3 $TESTPOOL/$TESTFS/shared3
log_must zfs set sharenfs=on $TESTPOOL/$TESTFS/shared1
log_must zfs set sharenfs=on $TESTPOOL/$TESTFS/shared2
log_must zfs set sharesmb=on $TESTPOOL/$TESTFS/shared2
log_must zfs set sharesmb=on $TESTPOOL/$TESTFS/shared3
log_must zfs share -a

# 2. Invoke 'zfs unshare nfs -a' to unshare filesystems.
log_must zfs unshare nfs -a

# 3. Verify that only nfs filesystems are unshared.
log_must not_shared $TESTPOOL/$TESTFS/shared1
log_must not_shared $TESTPOOL/$TESTFS/shared2
log_must is_shared_smb $TESTPOOL/$TESTFS/shared2
log_must is_shared_smb $TESTPOOL/$TESTFS/shared3

# 4. Share all filesystems again.
log_must zfs share -a

# 5. Invoke 'zfs unshare smb -a' and verify only smb filesystems are unshared.
log_must zfs unshare smb -a
log_must is_shared $TESTPOOL/$TESTFS/shared1
log_must is_shared $TESTPOOL/$TESTFS/shared2
log_must not_shared_smb $TESTPOOL/$TESTFS/shared2
log_must not_shared_smb $TESTPOOL/$TESTFS/shared3

log_pass "'zfs unshare [nfs|smb] -a' only works on the specified protocol."
