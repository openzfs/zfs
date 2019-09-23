#!/bin/ksh -p
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
# Copyright (c) 2019 by Lawrence Livermore National Security, LLC.
#

# DESCRIPTION:
#	Verify the hostid file can reside on a ZFS dataset.
#
# STRATEGY:
#	1. Create a non-redundant pool
#	2. Create an 'etc' dataset containing a valid hostid file
#	3. Create a file so the pool will have some contents
#	4. Verify multihost cannot be enabled until the /etc/hostid is linked
#	5. Verify vdevs may be attached and detached
#	6. Verify normal, cache, log and special vdevs can be added
#	7. Verify normal, cache, and log vdevs can be removed
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/mmp/mmp.cfg
. $STF_SUITE/tests/functional/mmp/mmp.kshlib

verify_runnable "both"

function cleanup
{
	default_cleanup_noexit
	log_must rm $MMP_DIR/file.{0,1,2,3,4,5}
	log_must rmdir $MMP_DIR
	log_must mmp_clear_hostid
	if [[ -L $HOSTID_FILE ]]; then
		rm -f $HOSTID_FILE
	fi
}

log_assert "Verify hostid file can reside on a ZFS dataset"
log_onexit cleanup

log_must mkdir -p $MMP_DIR
log_must truncate -s $MINVDEVSIZE $MMP_DIR/file.{0,1,2,3,4,5}

# 1. Create a non-redundant pool
log_must zpool create $MMP_POOL $MMP_DIR/file.0

# 2. Create an 'etc' dataset containing a valid hostid file; caching is
#    disabled on the dataset to force the hostid to be read from disk.
log_must zfs create -o primarycache=none -o secondarycache=none $MMP_POOL/etc
mntpnt_etc=$(get_prop mountpoint $MMP_POOL/etc)
log_must mmp_set_hostid $HOSTID1
log_must mv $HOSTID_FILE $mntpnt_etc/hostid

# 3. Create a file so the pool will have some contents
log_must zfs create $MMP_POOL/fs
mntpnt_fs=$(get_prop mountpoint $MMP_POOL/fs)
log_must mkfile 1M $mntpnt_fs/file

# 4. Verify multihost cannot be enabled until the /etc/hostid is linked
log_mustnot zpool set multihost=on $MMP_POOL
log_mustnot ls -l $HOSTID_FILE
log_must ln -s $mntpnt_etc/hostid $HOSTID_FILE
log_must zpool set multihost=on $MMP_POOL

# 5. Verify vdevs may be attached and detached
log_must zpool attach $MMP_POOL $MMP_DIR/file.0 $MMP_DIR/file.1
log_must zpool detach $MMP_POOL $MMP_DIR/file.1

# 6. Verify normal, cache, log and special vdevs can be added
log_must zpool add $MMP_POOL $MMP_DIR/file.1
log_must zpool add $MMP_POOL $MMP_DIR/file.2
log_must zpool add $MMP_POOL cache $MMP_DIR/file.3
log_must zpool add $MMP_POOL log $MMP_DIR/file.4
log_must zpool add $MMP_POOL special $MMP_DIR/file.5

# 7. Verify normal, cache, and log vdevs can be removed
log_must zpool remove $MMP_POOL $MMP_DIR/file.2
log_must zpool remove $MMP_POOL $MMP_DIR/file.3
log_must zpool remove $MMP_POOL $MMP_DIR/file.4

log_pass "Verify hostid file can reside on a ZFS dataset."
