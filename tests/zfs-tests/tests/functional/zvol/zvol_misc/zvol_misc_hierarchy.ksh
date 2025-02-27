#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright 2018, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# ZVOLs cannot have children datasets: verify zfs commands respect this
# hierarchy rule.
#
# STRATEGY:
# 1. Create a filesystem and a ZVOL
# 2. Verify 'zfs recv' will not (force-)receive a ZVOL over the root dataset
# 3. Verify 'zfs recv' cannot receive a ZVOL overwriting datasets with children
# 4. Verify 'zfs recv' cannot receive datasets below a ZVOL
# 5. Verify 'zfs create' cannot create datasets under a ZVOL
# 6. Verify 'zfs rename' cannot move datasets under a ZVOL
#

verify_runnable "both"

function cleanup
{
	destroy_pool "$poolname"
	log_must rm -f "$vdevfile" "$streamfile_fs" "$streamfile_zvol"
}

log_assert "ZVOLs cannot have children datasets: verify zfs commands respect "\
	"this hierarchy rule"
log_onexit cleanup

poolname="$TESTPOOL-zvol_hierarchy"
vdevfile="$TEST_BASE_DIR/vdevfile.$$"
streamfile_fs="$TEST_BASE_DIR/streamfile_fs.$$"
streamfile_zvol="$TEST_BASE_DIR/streamfile_zvol.$$"

# 1. Create filesystems and ZVOLs
# NOTE: set "mountpoint=none" just to speed up the test process
log_must truncate -s $MINVDEVSIZE "$vdevfile"
log_must zpool create -O mountpoint=none "$poolname" "$vdevfile"
log_must zfs create "$poolname/sendfs"
log_must zfs create -V 1M -s "$poolname/sendvol"
log_must zfs snapshot "$poolname/sendfs@snap"
log_must zfs snapshot "$poolname/sendvol@snap"
log_must eval "zfs send $poolname/sendfs@snap > $streamfile_fs"
log_must eval "zfs send $poolname/sendvol@snap > $streamfile_zvol"

# 2. Verify 'zfs recv' will not (force-)receive a ZVOL over the root dataset
log_mustnot eval "zfs receive -F $poolname < $streamfile_zvol"

# 3. Verify 'zfs recv' cannot receive a ZVOL overwriting datasets with children
log_must zfs create "$poolname/fs"
log_must zfs create "$poolname/fs/subfs"
log_mustnot eval "zfs receive -F $poolname/fs < $streamfile_zvol"
log_must zfs destroy "$poolname/fs/subfs"
log_must eval "zfs receive -F $poolname/fs < $streamfile_zvol"

# 4. Verify 'zfs recv' cannot receive datasets below a ZVOL
log_must zfs create -V 1M -s "$poolname/volume"
log_mustnot eval "zfs receive $poolname/volume/subfs < $streamfile_fs"
log_mustnot eval "zfs receive $poolname/volume/subvol < $streamfile_zvol"

# 5. Verify 'zfs create' cannot create datasets under a ZVOL
log_must zfs create -V 1M -s "$poolname/createvol"
log_mustnot zfs create "$poolname/createvol/fs"
log_mustnot zfs create -V 1M -s "$poolname/createvol/vol"

# 6. Verify 'zfs rename' cannot move datasets under a ZVOL
log_must zfs create "$poolname/movefs"
log_must zfs create -V 1M -s "$poolname/movevol"
log_must zfs create -V 1M -s "$poolname/renamevol"
log_mustnot zfs rename "$poolname/fs" "$poolname/renamevol/fs"
log_mustnot zfs rename "$poolname/vol" "$poolname/renamevol/vol"

log_pass "ZVOLs cannot have children datasets and zfs commands enforce this "\
	"rule"
