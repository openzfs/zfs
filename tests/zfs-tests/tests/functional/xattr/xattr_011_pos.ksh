#!/bin/ksh -p
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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/xattr/xattr_common.kshlib

#
# DESCRIPTION:
#
# Basic applications work with xattrs: cpio cp find mv pax tar
#
# STRATEGY:
#	1. For each application
#       2. Create an xattr and archive/move/copy/find files with xattr support
#	3. Also check that when appropriate flag is not used, the xattr
#	   doesn't get copied
#

function cleanup {

	log_must rm $TESTDIR/myfile.$$
}

log_assert "Basic applications work with xattrs: cpio cp find mv pax tar"
log_onexit cleanup

# Create a file, and set an xattr on it. This file is used in several of the
# test scenarios below.
log_must touch $TESTDIR/myfile.$$
create_xattr $TESTDIR/myfile.$$ passwd /etc/passwd


# For the archive applications below (tar, cpio, pax)
# we create two archives, one with xattrs, one without
# and try various cpio options extracting the archives
# with and without xattr support, checking for correct behaviour

if is_illumos; then
	log_note "Checking cpio"
	log_must touch $TESTDIR/cpio.$$
	create_xattr $TESTDIR/cpio.$$ passwd /etc/passwd
	echo $TESTDIR/cpio.$$ | cpio -o@ > $TEST_BASE_DIR/xattr.$$.cpio
	echo $TESTDIR/cpio.$$ | cpio -o > $TEST_BASE_DIR/noxattr.$$.cpio

	# we should have no xattr here
	log_must cpio -iu < $TEST_BASE_DIR/xattr.$$.cpio
	log_mustnot eval "runat $TESTDIR/cpio.$$ cat passwd > /dev/null 2>&1"

	# we should have an xattr here
	log_must cpio -iu@ < $TEST_BASE_DIR/xattr.$$.cpio
	log_must eval "runat $TESTDIR/cpio.$$ cat passwd > /dev/null 2>&1"

	# we should have no xattr here
	log_must cpio -iu < $TEST_BASE_DIR/noxattr.$$.cpio
	log_mustnot eval "runat $TESTDIR/cpio.$$ cat passwd > /dev/null 2>&1"

	# we should have no xattr here
	log_must cpio -iu@ < $TEST_BASE_DIR/noxattr.$$.cpio
	log_mustnot eval "runat $TESTDIR/cpio.$$ cat passwd > /dev/null 2>&1"
	log_must rm $TESTDIR/cpio.$$ $TEST_BASE_DIR/xattr.$$.cpio $TEST_BASE_DIR/noxattr.$$.cpio
else
	log_note "Checking cpio - unsupported"
fi

# check that with the right flag, the xattr is preserved
if is_freebsd; then
	log_note "Checking cp - unsupported"
elif is_linux; then
	log_note "Checking cp"
	log_must cp -a $TESTDIR/myfile.$$ $TESTDIR/myfile2.$$

	compare_xattrs $TESTDIR/myfile.$$ $TESTDIR/myfile2.$$ passwd
	log_must rm $TESTDIR/myfile2.$$

	# without the right flag, there should be no xattr
	log_must cp $TESTDIR/myfile.$$ $TESTDIR/myfile2.$$
	log_mustnot get_xattr passwd $TESTDIR/myfile2.$$
	log_must rm $TESTDIR/myfile2.$$
else
	log_note "Checking cp"
	log_must cp -@ $TESTDIR/myfile.$$ $TESTDIR/myfile2.$$

	compare_xattrs $TESTDIR/myfile.$$ $TESTDIR/myfile2.$$ passwd
	log_must rm $TESTDIR/myfile2.$$

	# without the right flag, there should be no xattr
	log_must cp $TESTDIR/myfile.$$ $TESTDIR/myfile2.$$
	log_mustnot eval "runat $TESTDIR/myfile2.$$ ls passwd > /dev/null 2>&1"
	log_must rm $TESTDIR/myfile2.$$
fi

# create a file without xattrs, and check that find -xattr only finds
# our test file that has an xattr.
if is_illumos; then
	log_note "Checking find"
	log_must mkdir $TESTDIR/noxattrs
	log_must touch $TESTDIR/noxattrs/no-xattr

	find $TESTDIR -xattr | grep myfile.$$
	[[ $? -ne 0 ]] && \
		log_fail "find -xattr didn't find our file that had an xattr."
	find $TESTDIR -xattr | grep no-xattr
	[[ $? -eq 0 ]] && \
		log_fail "find -xattr found a file that didn't have an xattr."
	log_must rm -rf $TESTDIR/noxattrs
else
	log_note "Checking find - unsupported"
fi

log_note "Checking mv"
# mv doesn't have any flags to preserve/omit xattrs - they're
# always moved.
log_must touch $TESTDIR/mvfile.$$
create_xattr $TESTDIR/mvfile.$$ passwd /etc/passwd
log_must mv $TESTDIR/mvfile.$$ $TESTDIR/mvfile2.$$
verify_xattr $TESTDIR/mvfile2.$$ passwd /etc/passwd
log_must rm $TESTDIR/mvfile2.$$

if is_illumos; then
	log_note "Checking pax"
	log_must touch $TESTDIR/pax.$$
	create_xattr $TESTDIR/pax.$$ passwd /etc/passwd
	log_must pax -w -f $TESTDIR/noxattr.pax $TESTDIR/pax.$$
	log_must pax -w@ -f $TESTDIR/xattr.pax $TESTDIR/pax.$$
	log_must rm $TESTDIR/pax.$$

	# we should have no xattr here
	log_must pax -r -f $TESTDIR/noxattr.pax
	log_mustnot eval "runat $TESTDIR/pax.$$ cat passwd > /dev/null 2>&1"
	log_must rm $TESTDIR/pax.$$

	# we should have no xattr here
	log_must pax -r@ -f $TESTDIR/noxattr.pax
	log_mustnot eval "runat $TESTDIR/pax.$$ cat passwd > /dev/null 2>&1"
	log_must rm $TESTDIR/pax.$$

	# we should have an xattr here
	log_must pax -r@ -f $TESTDIR/xattr.pax
	verify_xattr $TESTDIR/pax.$$ passwd /etc/passwd
	log_must rm $TESTDIR/pax.$$

	# we should have no xattr here
	log_must pax -r -f $TESTDIR/xattr.pax $TESTDIR
	log_mustnot eval "runat $TESTDIR/pax.$$ cat passwd > /dev/null 2>&1"
	log_must rm $TESTDIR/pax.$$ $TESTDIR/noxattr.pax $TESTDIR/xattr.pax
else
	log_note "Checking pax - unsupported"
fi

log_note "Checking tar"
if is_illumos; then
	log_must touch $TESTDIR/tar.$$
	create_xattr $TESTDIR/tar.$$ passwd /etc/passwd

	log_must cd $TESTDIR

	log_must tar cf noxattr.tar tar.$$
	log_must tar c@f xattr.tar tar.$$
	log_must rm $TESTDIR/tar.$$

	# we should have no xattr here
	log_must tar xf xattr.tar
	log_mustnot eval "runat $TESTDIR/tar.$$ cat passwd > /dev/null 2>&1"
	log_must rm $TESTDIR/tar.$$

	# we should have an xattr here
	log_must tar x@f xattr.tar
	verify_xattr tar.$$ passwd /etc/passwd
	log_must rm $TESTDIR/tar.$$

	# we should have no xattr here
	log_must tar xf $TESTDIR/noxattr.tar
	log_mustnot eval "runat $TESTDIR/tar.$$ cat passwd > /dev/null 2>&1"
	log_must rm $TESTDIR/tar.$$

	# we should have no xattr here
	log_must tar x@f $TESTDIR/noxattr.tar
	log_mustnot eval "runat $TESTDIR/tar.$$ cat passwd > /dev/null 2>&1"
	log_must rm $TESTDIR/tar.$$ $TESTDIR/noxattr.tar $TESTDIR/xattr.tar
else
	log_must touch $TESTDIR/tar.$$
	create_xattr $TESTDIR/tar.$$ passwd /etc/passwd

	log_must cd $TESTDIR

	log_must tar --no-xattrs -cf noxattr.tar tar.$$
	log_must tar --xattrs -cf xattr.tar tar.$$
	log_must rm $TESTDIR/tar.$$

	# we should have no xattr here
	log_must tar --no-xattrs -xf xattr.tar
	log_mustnot get_xattr passwd $TESTDIR/tar.$$
	log_must rm $TESTDIR/tar.$$

	# we should have an xattr here
	log_must tar --xattrs -xf xattr.tar
	verify_xattr tar.$$ passwd /etc/passwd
	log_must rm $TESTDIR/tar.$$

	# we should have no xattr here
	log_must tar --no-xattrs -xf $TESTDIR/noxattr.tar
	log_mustnot get_xattr passwd $TESTDIR/tar.$$
	log_must rm $TESTDIR/tar.$$

	# we should have no xattr here
	log_must tar --xattrs -xf $TESTDIR/noxattr.tar
	log_mustnot get_xattr passwd $TESTDIR/tar.$$
	log_must rm $TESTDIR/tar.$$ $TESTDIR/noxattr.tar $TESTDIR/xattr.tar
fi

log_assert "Basic applications work with xattrs: cpio cp find mv pax tar"
