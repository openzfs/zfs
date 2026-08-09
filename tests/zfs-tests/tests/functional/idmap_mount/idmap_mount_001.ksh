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

. $STF_SUITE/tests/functional/idmap_mount/idmap_mount_common.kshlib

#
#
# DESCRIPTION:
#       Test uid and gid of files in idmapped folder are mapped correctly
#
#
# STRATEGY:
#       1. Create files/folder owned by $UID1 and $GID1 under "idmap_test"
#       2. Idmap the folder to "idmap_dest"
#       3. Verify the owner of files/folder under "idmap_dest"
#

verify_runnable "global"

export WORKDIR=$TESTDIR/idmap_test
export IDMAPDIR=$TESTDIR/idmap_dest

function cleanup
{
	log_must rm -rf $WORKDIR
	if mountpoint $IDMAPDIR; then
		log_must umount $IDMAPDIR
	fi
	log_must rm -rf $IDMAPDIR
}

log_onexit cleanup

if ! idmap_util -c $TESTDIR; then
	log_unsupported "Idmap mount not supported."
fi

log_must mkdir -p $WORKDIR
log_must mkdir -p $IDMAPDIR
log_must touch $WORKDIR/file1
log_must mkdir $WORKDIR/subdir
log_must ln -s $WORKDIR/file1 $WORKDIR/file1_sym
log_must ln $WORKDIR/file1 $WORKDIR/subdir/file1_hard
log_must touch $WORKDIR/subdir/file2
log_must chown -R $UID1:$GID1 $WORKDIR
log_must chown $UID2:$GID2 $WORKDIR/subdir/file2

log_must idmap_util -m "u:${UID1}:${UID2}:1" -m "g:${GID1}:${GID2}:1" $WORKDIR $IDMAPDIR

log_must test "$UID2 $GID2" = "$(stat -c '%u %g' $IDMAPDIR/file1)"
log_must test "$UID2 $GID2" = "$(stat -c '%u %g' $IDMAPDIR/file1_sym)"
log_must test "$UID2 $GID2" = "$(stat -c '%u %g' $IDMAPDIR/subdir)"
log_must test "$UID2 $GID2" = "$(stat -c '%u %g' $IDMAPDIR/subdir/file1_hard)"
log_mustnot test "$UID2 $GID2" = "$(stat -c '%u %g' $IDMAPDIR/subdir/file2)"

log_pass "Owner verification of entries under idmapped folder is successful."

