#!/bin/ksh

#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2019 by Tomohiro Kusumi. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify root inode (directory) has correct link count.
#
# STRATEGY:
# 1. Create pool and fs.
# 2. Test link count of root inode.
# 3. Create directories and test link count of root inode.
# 4. Delete directories and test link count of root inode.
# 5. Create regular file and test link count of root inode.
# 6. Delete regular file and test link count of root inode.
#

function assert_link_count
{
	typeset dirpath="$1"
	typeset value="$2"

	log_must test "$(ls -ld $dirpath | awk '{ print $2 }')" == "$value"
}

verify_runnable "both"

log_note "Verify root inode (directory) has correct link count."

# Delete a directory from link_count_001.ksh.
if [ -d "${TESTDIR}" -a -d "${TESTDIR}/tmp" ]; then
	log_must rm -rf ${TESTDIR}/tmp
fi

#
# Test with hidden '.zfs' directory.
# This also tests general directories.
#
log_note "Testing with snapdir set to hidden (default)"

for dst in $TESTPOOL $TESTPOOL/$TESTFS
do
	typeset mtpt=$(get_prop mountpoint $dst)
	log_must zfs set snapdir=hidden $dst
	log_must test -d "$mtpt/.zfs"
	if test -n "$(ls $mtpt)"; then
		ls $mtpt
		log_note "$mtpt not empty, skipping"
		continue
	fi
	assert_link_count $mtpt 2

	log_must mkdir $mtpt/a
	assert_link_count $mtpt 3
	log_must rmdir $mtpt/a
	assert_link_count $mtpt 2

	log_must mkdir -p $mtpt/a/b
	assert_link_count $mtpt 3
	log_must rmdir $mtpt/a/b
	log_must rmdir $mtpt/a
	assert_link_count $mtpt 2

	log_must touch $mtpt/a
	assert_link_count $mtpt 2
	log_must rm $mtpt/a
	assert_link_count $mtpt 2
done

#
# Test with visible '.zfs' directory.
#
log_note "Testing with snapdir set to visible"

for dst in $TESTPOOL $TESTPOOL/$TESTFS
do
	typeset mtpt=$(get_prop mountpoint $dst)
	log_must zfs set snapdir=visible $dst
	log_must test -d "$mtpt/.zfs"
	if test -n "$(ls $mtpt)"; then
		ls $mtpt
		log_note "$mtpt not empty, skipping"
		continue
	fi
	assert_link_count $mtpt 3

	log_must mkdir $mtpt/a
	assert_link_count $mtpt 4
	log_must rmdir $mtpt/a
	assert_link_count $mtpt 3

	log_must mkdir -p $mtpt/a/b
	assert_link_count $mtpt 4
	log_must rmdir $mtpt/a/b
	log_must rmdir $mtpt/a
	assert_link_count $mtpt 3

	log_must touch $mtpt/a
	assert_link_count $mtpt 3
	log_must rm $mtpt/a
	assert_link_count $mtpt 3
done

log_pass "Verify root inode (directory) has correct link count passed"
