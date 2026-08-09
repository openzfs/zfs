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

#
# Portions Copyright 2021 iXsystems, Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/acl/acl_common.kshlib

#
# DESCRIPTION:
#	Verify that POSIX mode bits function correctly.
#
#	These tests are incomplete and will be added to over time.
#
#	NOTE: Creating directory entries behaves differently between platforms.
#	The parent directory's group is used on FreeBSD, while the effective
#	group is used on Linux.  We chown to the effective group when creating
#	directories and files in these tests to achieve consistency across all
#	platforms.
#
# STRATEGY:
#	1. Sanity check the POSIX mode test on tmpfs
#	2. Test POSIX mode bits on ZFS
#

verify_runnable "both"

function cleanup
{
	umount -f $tmpdir
	rm -rf $tmpdir $TESTDIR/dir
}

log_assert "Verify POSIX mode bits function correctly"
log_onexit cleanup

owner=$ZFS_ACL_STAFF1
other=$ZFS_ACL_STAFF2
group=$ZFS_ACL_STAFF_GROUP
if is_linux; then
	wheel=root
else
	wheel=wheel
fi

function test_posix_mode # base
{
	typeset base=$1
	typeset dir=$base/dir
	typeset file=$dir/file

	# dir owned by root
	log_must mkdir $dir
	log_must chown :$wheel $dir
	log_must chmod 007 $dir

	# file owned by root
	log_must touch $file
	log_must chown :$wheel $file
	log_must ls -la $dir
	log_must rm $file

	log_must touch $file
	log_must chown :$wheel $file
	log_must user_run $other rm $file

	# file owned by user
	log_must user_run $owner touch $file
	log_must chown :$group $file
	log_must ls -la $dir
	log_must user_run $owner rm $file

	log_must user_run $owner touch $file
	log_must chown :$group $file
	log_must user_run $other rm $file

	log_must user_run $owner touch $file
	log_must chown :$group $file
	log_must rm $file

	log_must rm -rf $dir

	# dir owned by user
	log_must user_run $owner mkdir $dir
	log_must chown :$group $dir
	log_must user_run $owner chmod 007 $dir

	# file owned by root
	log_must touch $file
	log_must chown :$wheel $file
	log_must ls -la $dir
	log_must rm $file

	log_must touch $file
	log_must chown :$wheel $file
	log_mustnot user_run $other rm $file
	log_must rm $file

	# file owned by user
	log_mustnot user_run $owner touch $file
	log_must touch $file
	log_must chown $owner:$group $file
	log_must ls -la $dir
	log_mustnot user_run $owner rm $file
	log_mustnot user_run $other rm $file
	log_must rm $file

	log_must rm -rf $dir
}

# Sanity check on tmpfs first
tmpdir=$(mktemp -d)
log_must mount -t tmpfs tmp $tmpdir
log_must chmod 777 $tmpdir

test_posix_mode $tmpdir

log_must umount $tmpdir
log_must rmdir $tmpdir

# Verify ZFS
test_posix_mode $TESTDIR

log_pass "POSIX mode bits function correctly"
