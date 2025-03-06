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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/xattr/xattr_common.kshlib

#
# DESCRIPTION:
#
# Create files on ufs|ext, copy those files to ZFS with appropriate cp flags,
# and verify the xattrs will still be readable.
#
# STRATEGY:
#	1. Create files in ufs|ext with xattrs
#	2. Copy those files to zfs
#	3. Ensure the xattrs can be read and written
#	4. Do the same in reverse.
#

# we need to be able to create zvols to hold our test ufs|ext filesystem.
verify_runnable "global"

# Make sure we clean up properly
function cleanup {
	if ismounted /tmp/$NEWFS_DEFAULT_FS.$$ $NEWFS_DEFAULT_FS; then
		log_must umount /tmp/$NEWFS_DEFAULT_FS.$$
	fi
	log_must rm -rf /tmp/$NEWFS_DEFAULT_FS.$$
}

log_assert "Files from $NEWFS_DEFAULT_FS with xattrs copied to zfs retain xattr info."
log_onexit cleanup

# Create a ufs|ext file system that we can work in
log_must zfs create -V128m $TESTPOOL/$TESTFS/zvol
block_device_wait
log_must eval "new_fs $ZVOL_DEVDIR/$TESTPOOL/$TESTFS/zvol > /dev/null 2>&1"

log_must mkdir /tmp/$NEWFS_DEFAULT_FS.$$
if is_illumos; then
	log_must mount $ZVOL_DEVDIR/$TESTPOOL/$TESTFS/zvol \
	    /tmp/$NEWFS_DEFAULT_FS.$$

	# Create files in ufs, and set some xattrs on them.
	log_must touch /tmp/$NEWFS_DEFAULT_FS.$$/$NEWFS_DEFAULT_FS-file.$$

	log_must runat /tmp/$NEWFS_DEFAULT_FS.$$/$NEWFS_DEFAULT_FS-file.$$ \
	     cp /etc/passwd .

	# copy those files to ZFS
	log_must cp -@ /tmp/$NEWFS_DEFAULT_FS.$$/$NEWFS_DEFAULT_FS-file.$$ \
	    $TESTDIR

	# ensure the xattr information has been copied correctly
	log_must runat $TESTDIR/$NEWFS_DEFAULT_FS-file.$$ \
	    diff passwd /etc/passwd

	log_must umount /tmp/$NEWFS_DEFAULT_FS.$$
else
	if is_linux; then
		options="-o user_xattr"
	fi
	log_must mount ${options:+""} \
	    $ZVOL_DEVDIR/$TESTPOOL/$TESTFS/zvol /tmp/$NEWFS_DEFAULT_FS.$$

	# Create files in ext, and set some xattrs on them.
	# Use small values for xattrs for ext compatibility.
	log_must touch /tmp/$NEWFS_DEFAULT_FS.$$/$NEWFS_DEFAULT_FS-file.$$

	echo "TEST XATTR" >/tmp/xattr1

	log_must set_xattr_stdin xattr1 \
	    /tmp/$NEWFS_DEFAULT_FS.$$/$NEWFS_DEFAULT_FS-file.$$ </tmp/xattr1

	# copy those files to ZFS
	if is_freebsd; then
		# cp does not preserve extattrs on FreeBSD
		export TAPE="-"
		log_must eval "tar cC /tmp/$NEWFS_DEFAULT_FS.$$ \
		    $NEWFS_DEFAULT_FS-file.$$ | tar xC $TESTDIR"
	else
		log_must cp -a \
		    /tmp/$NEWFS_DEFAULT_FS.$$/$NEWFS_DEFAULT_FS-file.$$ \
		    $TESTDIR
	fi

	# ensure the xattr information has been copied correctly
	log_must eval "get_xattr xattr1 $TESTDIR/$NEWFS_DEFAULT_FS-file.$$ \
	    >/tmp/xattr1.$$"
	log_must diff /tmp/xattr1.$$ /tmp/xattr1
	log_must rm $TESTDIR/$NEWFS_DEFAULT_FS-file.$$
	log_must rm /tmp/xattr1 /tmp/xattr1.$$

	log_must umount /tmp/$NEWFS_DEFAULT_FS.$$
fi

log_pass "Files from $NEWFS_DEFAULT_FS with xattrs copied to zfs retain xattr info."
