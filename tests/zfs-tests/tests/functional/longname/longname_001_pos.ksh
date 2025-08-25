#! /bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Verify the support for long filenames.
#
# STRATEGY:
# 0.  On a fresh dataset ensure property "longname" is enabled by default.
# 1.  Disable the longname.
# 2.  Try to create a filename whose length is > 256 bytes. This should fail.
# 3.  Enable "longname" property on the dataset.
# 4.  Try to create files and dirs whose names are > 256 bytes.
# 5.  Ensure that "ls" is able to list the file.
# 6.  Ensure stat(1) is able to stat the file/directory.
# 7.  Try to rename the files and directories
# 8.  Try to delete the files and directories

verify_runnable "global"

WORKDIR=$TESTDIR/workdir
MOVEDIR=$TESTDIR/movedir/level2/level3/level4/level5/level6

function cleanup
{
        log_must rm -rf $WORKDIR
        log_must rm -rf $TESTDIR/movedir
}

LONGNAME=$(printf 'a%.0s' {1..512})
LONGFNAME="file-$LONGNAME"
LONGDNAME="dir-$LONGNAME"
LONGPNAME="mypipe-$LONGNAME"
LONGCNAME="char_dev-$LONGNAME"
LONGLNAME="link-$LONGNAME"
LONGNAME_255=$(printf 'a%.0s' {1..255})
LONGNAME_1023=$(printf 'a%.0s' {1..1023})


log_assert "Check longname support for directories/files"

log_onexit cleanup

log_must mkdir $WORKDIR
log_must mkdir -p $MOVEDIR

# Disable longname support
log_must zfs set longname=off $TESTPOOL/$TESTFS

#Ensure a file of length 255bytes can be created
log_must touch $WORKDIR/$LONGNAME_255

#Where as file of length 256bytes should fail
log_mustnot touch $WORKDIR/${LONGNAME_255}b

# Try to create a file with long name with property "longname=off"
log_mustnot touch $WORKDIR/$LONGFNAME
log_mustnot mkdir $WORKDIR/$LONGDNAME

# Enable longname support
log_must zfs set longname=on $TESTPOOL/$TESTFS

# Retry the longname creates and that should succeed
log_must mkdir $WORKDIR/$LONGDNAME
log_must touch $WORKDIR/$LONGFNAME

# Should be able to create a file with name of 1023 chars
log_must touch $WORKDIR/$LONGNAME_1023

# And names longer that 1023 should fail
log_mustnot touch $WORKDIR/${LONGNAME_1023}b

# Ensure the longnamed dir/file can be listed.
name=$(ls $WORKDIR/$LONGFNAME)
if [[ "${name}" != "$WORKDIR/$LONGFNAME" ]]; then
        log_fail "Unable to list: $WORKDIR/$LONGFNAME ret:$name"
fi

name=$(ls -d $WORKDIR/$LONGDNAME)
if [[ "${name}" != "$WORKDIR/$LONGDNAME" ]]; then
        log_fail "Unable to list: $WORKDIR/$LONGDNAME ret:$name"
fi

# Ensure stat works
log_must stat $WORKDIR/$LONGFNAME
log_must stat $WORKDIR/$LONGDNAME

# Ensure softlinks can be created from a longname to
# another longnamed file.
log_must ln -s $WORKDIR/$LONGFNAME $WORKDIR/$LONGLNAME

# Ensure a longnamed pipe and character device file
# can be created
log_must mknod $WORKDIR/$LONGPNAME p
log_must mknod $WORKDIR/$LONGCNAME c 92 1

# Ensure we can rename the longname file
log_must mv $WORKDIR/$LONGFNAME $WORKDIR/file2

# Delete the long named dir/file
log_must rmdir $WORKDIR/$LONGDNAME
log_must rm $WORKDIR/file2
log_must rm $WORKDIR/$LONGPNAME
log_must rm $WORKDIR/$LONGCNAME
log_must rm $WORKDIR/$LONGLNAME

log_pass
