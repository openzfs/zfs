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

. $STF_SUITE/tests/functional/userquota/projectmac.kshlib

#
# DESCRIPTION:
# Rolling an encrypted filesystem back to a snapshot in the other form gives
# it that form, until its next write converts it to the form the projectmac
# pool property sets.
#
# STRATEGY:
# 1. Create and write an encrypted filesystem in the platform default form,
#    and snapshot it
# 2. Set projectmac to the other form, check that the filesystem was
#    converted but not its snapshot, and write to it
# 3. Unmount the filesystem, so that nothing writes it, roll it back to the
#    snapshot and check that it has the snapshot's form
# 4. Mount and write the filesystem, and check that it was converted
# 5. Export and import the pool, and check that the forms were kept
#

verify_runnable "global"

POOLNAME=testpool$$
FILEDEV=$TEST_BASE_DIR/userspace_encrypted_projectmac_rollback
FS=$POOLNAME/fs

function cleanup
{
	destroy_pool $POOLNAME
	rm -f $FILEDEV
}

log_onexit cleanup

log_assert "Rollback restores the snapshot's form until the next write"

truncate -s $SPA_MINDEVSIZE $FILEDEV
log_must zpool create $POOLNAME $FILEDEV
form=$(projectmac_default $POOLNAME)
other=$(projectmac_other $form)

# 1.
create_encrypted $FS
log_must zfs snapshot $FS@snap

# 2.
log_must zpool set projectmac=$other $POOLNAME
check_projectmac $FS $other
check_projectmac $FS@snap $form
write_and_sync $FS

# 3.
log_must zfs unmount $FS
log_must zfs rollback $FS@snap
check_projectmac $FS $form

# 4.
log_must zfs mount $FS
write_and_sync $FS
check_projectmac $FS $other

# 5.
log_must zpool export $POOLNAME
log_must zpool import -d $TEST_BASE_DIR $POOLNAME
load_and_mount $FS
check_projectmac $FS $other
check_projectmac $FS@snap $form
log_must eval "zfs send $FS@snap > /dev/null"
log_must zfs projectspace $FS

log_pass "Rollback restores the snapshot's form until the next write"
