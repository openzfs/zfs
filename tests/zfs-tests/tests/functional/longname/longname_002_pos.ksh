#! /bin/ksh -p
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
# Copyright (c) 2021 by Nutanix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Check if longname feature is disabled by default and can be enabled.
#
# STRATEGY:
# 1. Create a zpool with longname feature disabled
# 2. Attempt to enable 'longname' property should fail.
# 3. Attempt to create a longnamed (>255) file should fail.
# 4. Enable the feature@longname
# 5. Enable 'longname' property on the dataset.
# 6. Should be able to create long named files and directories.
# 7. Should be able to disable longname property.
# 8. This should disallow creating new longnamed file/dirs. But, should be
#    able to access existing longnamed files/dirs.
verify_runnable "global"

function cleanup
{
        log_must rm -rf $WORKDIR
        poolexists $TESTPOOL && zpool destroy $TESTPOOL
}

log_assert "Check feature@longname and 'longname' dataset propery work correctly"

log_onexit cleanup

log_must zpool destroy $TESTPOOL

log_must zpool create -o feature@longname=disabled $TESTPOOL $DISKS

log_must zfs create $TESTPOOL/$TESTFS2

log_must zfs set mountpoint=$TESTDIR2 $TESTPOOL/$TESTFS2

log_mustnot zfs set longname=on $TESTPOOL/$TESTFS2

LONGNAME=$(printf 'a%.0s' {1..512})
LONGFNAME="file-$LONGNAME"
LONGDNAME="dir-$LONGNAME"
SHORTDNAME="dir-short"
SHORTFNAME="file-short"
WORKDIR=$TESTDIR2/workdir

log_must mkdir $WORKDIR
log_mustnot touch $WORKDIR/$LONGFNAME
log_mustnot mkdir $WORKDIR/$LONGDNAME

log_must zpool set feature@longname=enabled $TESTPOOL
log_must zfs set longname=on $TESTPOOL/$TESTFS2

log_must mkdir $WORKDIR/$LONGDNAME
log_must touch $WORKDIR/$LONGFNAME

# Ensure the above changes are synced out.
log_must zpool sync $TESTPOOL

# Ensure that the feature is activated once longnamed files are created.
state=$(zpool get feature@longname -H -o value $TESTPOOL)
log_note "feature@longname on pool: $TESTPOOL : $state"

if [[ "$state" != "active" ]]; then
	log_fail "feature@longname has state $state (expected active)"
fi

# Set longname=off.
log_must zfs set longname=off $TESTPOOL/$TESTFS2

# Ensure no new file/directory with longnames can be created or can be renamed
# to.
log_mustnot mkdir $WORKDIR/${LONGDNAME}.1
log_mustnot touch $WORKDIR/${LONGFNAME}.1
log_must mkdir $WORKDIR/$SHORTDNAME
log_mustnot mv $WORKDIR/$SHORTDNAME $WORKDIR/${LONGDNAME}.1
log_must touch $WORKDIR/$SHORTFNAME
log_mustnot mv $WORKDIR/$SHORTFNAME $WORKDIR/${LONGFNAME}.1

#Cleanup shortnames
log_must rmdir $WORKDIR/$SHORTDNAME
log_must rm $WORKDIR/$SHORTFNAME

# But, should be able to stat and rename existing files
log_must stat $WORKDIR/$LONGDNAME
log_must stat $WORKDIR/$LONGFNAME
log_must mv $WORKDIR/$LONGDNAME $WORKDIR/$SHORTDNAME
log_must mv $WORKDIR/$LONGFNAME $WORKDIR/$SHORTFNAME

log_pass
