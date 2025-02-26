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

. $STF_SUITE/tests/functional/idmap_mount/idmap_mount_common.kshlib

#
#
# DESCRIPTION:
#       Perform file operations in idmapped folder in user namespace,
#       then check the owner in its base.
#
#
# STRATEGY:
#       1. Create folder "idmap_test"
#       2. Idmap the folder to "idmap_dest"
#       3. Perform file operations in the idmapped folder in the user
#          namespace having the same idmap info as the idmapped mount
#       4. Verify the owner of entries under the base folder "idmap_test"
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
log_must idmap_util -m "u:${UID1}:${UID2}:1" -m "g:${GID1}:${GID2}:1" $WORKDIR $IDMAPDIR

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

log_must $NSENTER touch $IDMAPDIR/file1
log_must test "$UID1 $GID1" = "$(stat -c '%u %g' $WORKDIR/file1)"

log_must $NSENTER mv $IDMAPDIR/file1 $IDMAPDIR/file1_renamed
log_must test "$UID1 $GID1" = "$(stat -c '%u %g' $WORKDIR/file1_renamed)"

log_must $NSENTER mv $IDMAPDIR/file1_renamed $IDMAPDIR/file1
log_must test "$UID1 $GID1" = "$(stat -c '%u %g' $WORKDIR/file1)"

log_must $NSENTER mkdir $IDMAPDIR/subdir
log_must test "$UID1 $GID1" = "$(stat -c '%u %g' $WORKDIR/subdir)"

log_must $NSENTER ln -s $IDMAPDIR/file1 $IDMAPDIR/file1_sym
log_must test "$UID1 $GID1" = "$(stat -c '%u %g' $WORKDIR/file1_sym)"

log_must $NSENTER ln $IDMAPDIR/file1 $IDMAPDIR/subdir/file1_hard
log_must test "$UID1 $GID1" = "$(stat -c '%u %g' $WORKDIR/subdir/file1_hard)"

log_must $NSENTER touch $IDMAPDIR/subdir/file2
log_must $NSENTER chown $UID1:$GID1 $IDMAPDIR/subdir/file2
log_mustnot $NSENTER chown $UID2 $IDMAPDIR/subdir/file2

log_must $NSENTER cp -r $IDMAPDIR/subdir $IDMAPDIR/subdir1
log_must test "$UID1 $GID1" = "$(stat -c '%u %g' $WORKDIR/subdir1/file2)"
log_must $NSENTER rm -rf $IDMAPDIR/subdir1

log_must $NSENTER cp -rp $IDMAPDIR/subdir $IDMAPDIR/subdir1
log_must test "$UID1 $GID1" = "$(stat -c '%u %g' $WORKDIR/subdir1/file1_hard)"
log_must $NSENTER rm -rf $IDMAPDIR/subdir1

log_pass "Owner verification of entries under the base folder is successful."

