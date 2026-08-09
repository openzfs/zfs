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
#       Test setgid bit is set properly on the idmapped mount
#       in a user namespace.
#
# STRATEGY:
#       1. Create folder "idmap_test", set gid bit on it
#       2. Idmap the folder to "idmap_dest"
#       3. Create file and folder in the idmapped folder in the user
#          namespace having the same idmap info 
#       4. Verify the gid bit of the file and folder is set
#

verify_runnable "global"

export WORKDIR=$TESTDIR/idmap_test
export IDMAPDIR=$TESTDIR/idmap_dest

function cleanup
{
	kill -TERM ${unshared_pid}
	log_must rm -rf $IDMAPDIR/*
	if mountpoint $IDMAPDIR; then
		log_must umount $IDMAPDIR
	fi
	log_must rm -rf $IDMAPDIR $WORKDIR
}

log_onexit cleanup

if ! idmap_util -c $TESTDIR; then
	log_unsupported "Idmap mount not supported."
fi

log_must mkdir -p $WORKDIR
log_must mkdir -p $IDMAPDIR

log_must chown $UID1:$GID1 $WORKDIR
# set gid bit
log_must chmod 2755 $WORKDIR
log_must idmap_util -m "u:${UID1}:${UID2}:1" -m "g:${GID1}:${GID2}:1" $WORKDIR $IDMAPDIR
log_must test -g $IDMAPDIR

# Create a user namespace with the same idmapping
unshare -Urm echo test
if [ "$?" -ne "0" ]; then
	log_unsupported "Failed to create user namespace"
fi
unshare -Um /usr/bin/sleep 2h &
unshared_pid=$!
if [ "$?" -ne "0" ]; then
	log_unsupported "Failed to create user namespace"
fi
# wait for userns to be ready
sleep 1
echo "${UID1} ${UID2} 1" > /proc/$unshared_pid/uid_map
if [ "$?" -ne "0" ]; then
	log_unsupported "Failed to write to uid_map"
fi
echo "${GID1} ${GID2} 1" > /proc/$unshared_pid/gid_map
if [ "$?" -ne "0" ]; then
	log_unsupported "Failed to write to gid_map"
fi

NSENTER="nsenter -t $unshared_pid --all -S ${UID1} -G ${GID1}"

# gid bit can be set on the file
log_must $NSENTER touch $IDMAPDIR/file1
log_must $NSENTER chmod 2654 $IDMAPDIR/file1
log_must test -g $WORKDIR/file1
log_must test -g $IDMAPDIR/file1
log_must test "$UID1 $GID1" = "$($NSENTER stat -c '%u %g' $IDMAPDIR/file1)"

# gid bit is carried over to new folder
log_must $NSENTER mkdir $IDMAPDIR/subdir
log_must test -g $WORKDIR/subdir
log_must test -g $IDMAPDIR/subdir
log_must test "$UID1 $GID1" = "$($NSENTER stat -c '%u %g' $IDMAPDIR/subdir)"

log_pass "Verification of setting gid bit in userns is successful."

