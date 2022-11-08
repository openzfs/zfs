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

. $STF_SUITE/tests/functional/idmap_mount/idmap_mount_common.kshlib

#
#
# DESCRIPTION:
#       Test idmapped mount in a user namespace
#
# STRATEGY:
#	1. Create a zoned dataset
#	2. Create a user namespace and designate the dataset to the zone
#	3. In the zone, mount the dataset to "idmap_test"
#	4. In the zone, idmap mount the dataset mountpoint to "idmap_dest"
#	5. Do some file operations in the idmapped mountpoint "idmap_dest"
#	6. Check the owner of files/folder in the mount point "idmap_test"
#	7. unmount the mountpoints in the zone
#	8. Remount the dataset in global zone to "idmap_test"
#	9. Check the owenr of filers/folder in the mountpoint "idmap_test"
#

verify_runnable "global"

export WORKDIR=$TESTDIR/idmap_test
export IDMAPDIR=$TESTDIR/idmap_dest

function cleanup
{
	if [[ -v unshared_pid ]]; then
		zfs unzone /proc/$unshared_pid/ns/user "$TESTPOOL/userns"
		kill -TERM ${unshared_pid}
	fi
	if mountpoint $WORKDIR; then
		log_must umount $WORKDIR
	fi
	log_must rm -rf $WORKDIR
}

log_onexit cleanup

if ! idmap_util -c $TESTDIR; then
	log_unsupported "Idmap mount not supported."
fi

unshare -Urm echo test
if [ "$?" -ne "0" ]; then
	log_unsupported "Failed to create user namespace"
fi

log_must zfs create -o zoned=off -o mountpoint=$WORKDIR "$TESTPOOL/userns"

# "root" user and group in the user ns
log_must chown 1000000:1000000 $WORKDIR
log_must zfs set zoned=on "$TESTPOOL/userns"

log_must mkdir -p $IDMAPDIR

unshare -Um /usr/bin/sleep 2h &
unshared_pid=$!
if [ "$?" -ne "0" ]; then
	log_unsupported "Failed to create user namespace"
fi
# wait for userns to be ready
sleep 1
echo "0 1000000 1000000" > /proc/$unshared_pid/uid_map
if [ "$?" -ne "0" ]; then
	log_unsupported "Failed to write to uid_map"
fi
echo "0 1000000 1000000" > /proc/$unshared_pid/gid_map
if [ "$?" -ne "0" ]; then
	log_unsupported "Failed to write to gid_map"
fi

NSENTER="nsenter -t $unshared_pid --all -S 0 -G 0"

log_must zfs zone /proc/$unshared_pid/ns/user "$TESTPOOL/userns"
log_must $NSENTER zfs mount "$TESTPOOL/userns"
log_must $NSENTER chmod 777 $WORKDIR

$NSENTER idmap_util -c $WORKDIR
if [ "$?" -ne "0" ]; then
	log_unsupported "Idmapped mount not supported in a user namespace"
fi

log_must $NSENTER idmap_util -m b:0:10000:100000 $WORKDIR $IDMAPDIR
log_must $NSENTER setpriv --reuid 11000 --regid 11000 --clear-groups touch $IDMAPDIR/file
log_must $NSENTER setpriv --reuid 11000 --regid 11000 --clear-groups mkdir $IDMAPDIR/folder
log_must $NSENTER setpriv --reuid 11000 --regid 11000 --clear-groups ln -s file $IDMAPDIR/file-soft
log_must $NSENTER setpriv --reuid 11000 --regid 11000 --clear-groups ln $IDMAPDIR/file $IDMAPDIR/file-hard

log_must $NSENTER setpriv --reuid 11000 --regid 11000 --clear-groups cp -p $IDMAPDIR/file $IDMAPDIR/folder/file-p
log_must $NSENTER setpriv --reuid 11000 --regid 11000 --clear-groups cp $IDMAPDIR/file $IDMAPDIR/folder/file

log_must test "1000 1000" = "$($NSENTER stat -c '%u %g' $WORKDIR/file)"
log_must test "1000 1000" = "$($NSENTER stat -c '%u %g' $WORKDIR/folder)"
log_must test "1000 1000" = "$($NSENTER stat -c '%u %g' $WORKDIR/file-soft)"
log_must test "1000 1000" = "$($NSENTER stat -c '%u %g' $WORKDIR/file-hard)"
log_must test "1000 1000" = "$($NSENTER stat -c '%u %g' $WORKDIR/folder/file-p)"
log_must test "1000 1000" = "$($NSENTER stat -c '%u %g' $WORKDIR/folder/file)"

log_must $NSENTER umount $IDMAPDIR
log_must $NSENTER umount $WORKDIR

log_must zfs unzone /proc/$unshared_pid/ns/user "$TESTPOOL/userns"
log_must kill -TERM $unshared_pid
unset unshared_pid
log_must zfs set zoned=off "$TESTPOOL/userns"
log_must zfs mount "$TESTPOOL/userns"

log_must test "1001000 1001000" = "$(stat -c '%u %g' $WORKDIR/file)"
log_must test "1001000 1001000" = "$(stat -c '%u %g' $WORKDIR/folder)"
log_must test "1001000 1001000" = "$(stat -c '%u %g' $WORKDIR/file-soft)"
log_must test "1001000 1001000" = "$(stat -c '%u %g' $WORKDIR/file-hard)"
log_must test "1001000 1001000" = "$(stat -c '%u %g' $WORKDIR/folder/file-p)"
log_must test "1001000 1001000" = "$(stat -c '%u %g' $WORKDIR/folder/file)"

log_pass "Testing idmapped mount in a user ns is successful."

