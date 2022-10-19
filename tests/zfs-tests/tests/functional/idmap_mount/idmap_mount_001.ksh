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

