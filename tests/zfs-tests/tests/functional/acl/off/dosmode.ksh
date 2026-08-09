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
#	Verify that DOS mode flags function correctly.
#
#
# STRATEGY:
#	1. ARCHIVE
#	2. HIDDEN
#	3. OFFLINE
#	4. READONLY
#	5. REPARSE
#	6. SPARSE
#	7. SYSTEM
#

verify_runnable "both"

function cleanup
{
	rm -f $testfile
}

function hasflag
{
	typeset flag=$1
	typeset path=$2

	if is_linux; then
		read_dos_attributes $path
	else
		ls -lo $path | awk '{ print $5 }'
	fi | grep -qwF $flag
}

log_assert "Verify DOS mode flags function correctly"
log_onexit cleanup

testfile=$TESTDIR/testfile
owner=$ZFS_ACL_STAFF1
other=$ZFS_ACL_STAFF2

if is_linux; then
	changeflags=write_dos_attributes
else
	changeflags=chflags
fi

#
# ARCHIVE
#
# This flag is set by ZFS when a file has been updated to indicate that
# the file needs to be archived.
#
log_must touch $testfile
log_must hasflag uarch $testfile
log_must $changeflags nouarch $testfile
log_must hasflag - $testfile
log_must touch $testfile
if ! is_linux; then
	log_must hasflag uarch $testfile
fi
log_must rm $testfile
log_must user_run $owner touch $testfile
log_must hasflag uarch $testfile
log_must user_run $owner $changeflags nouarch $testfile
log_mustnot user_run $other $changeflags uarch $testfile
log_must hasflag - $testfile
log_must user_run $owner touch $testfile
log_mustnot user_run $other $changeflags nouarch $testfile
if ! is_linux; then
	log_must hasflag uarch $testfile
fi
log_must user_run $owner rm $testfile

#
# HIDDEN
#
log_must touch $testfile
log_must $changeflags hidden $testfile
log_must hasflag hidden $testfile
log_must $changeflags 0 $testfile
log_must hasflag - $testfile
log_must rm $testfile
log_must user_run $owner touch $testfile
log_must user_run $owner $changeflags hidden $testfile
log_mustnot user_run $other $changeflags nohidden $testfile
log_must hasflag hidden $testfile
log_must user_run $owner $changeflags 0 $testfile
log_mustnot user_run $other $changeflags hidden $testfile
log_must hasflag - $testfile
log_must user_run $owner rm $testfile


#
# OFFLINE
#
log_must touch $testfile
log_must $changeflags offline $testfile
log_must hasflag offline $testfile
log_must $changeflags 0 $testfile
log_must hasflag - $testfile
log_must rm $testfile
log_must user_run $owner touch $testfile
log_must user_run $owner $changeflags offline $testfile
log_mustnot user_run $other $changeflags nooffline $testfile
log_must hasflag offline $testfile
log_must user_run $owner $changeflags 0 $testfile
log_mustnot user_run $other $changeflags offline $testfile
log_must hasflag - $testfile
log_must user_run $owner rm $testfile

#
# READONLY
#
# This flag prevents users from writing or appending to the file,
# but root is always allowed the operation.
#
log_must touch $testfile
log_must $changeflags rdonly $testfile
log_must hasflag rdonly $testfile
log_must eval "echo 'root write allowed' >> $testfile"
log_must cat $testfile
log_must $changeflags 0 $testfile
log_must hasflag - $testfile
log_must rm $testfile
# It is required to still be able to write to an fd that was opened RW before
# READONLY is set.  We have a special test program for that.
log_must user_run $owner touch $testfile
log_mustnot user_run $other $changeflags rdonly $testfile
log_must user_run $owner dosmode_readonly_write $testfile
log_mustnot user_run $other $changeflags nordonly $testfile
log_must hasflag rdonly $testfile
if ! is_linux; then
	log_mustnot user_run $owner "echo 'user write forbidden' >> $testfile"
fi
log_must eval "echo 'root write allowed' >> $testfile"
# We are still allowed to read and remove the file when READONLY is set.
log_must user_run $owner cat $testfile
log_must user_run $owner rm $testfile

#
# REPARSE
#
# not allowed to be changed

#
# SPARSE
#
log_must truncate -s 1m $testfile
log_must $changeflags sparse $testfile
log_must hasflag sparse $testfile
log_must $changeflags 0 $testfile
log_must hasflag - $testfile
log_must rm $testfile
log_must user_run $owner truncate -s 1m $testfile
log_must user_run $owner $changeflags sparse $testfile
log_mustnot user_run $other $changeflags nosparse $testfile
log_must hasflag sparse $testfile
log_must user_run $owner $changeflags 0 $testfile
log_mustnot user_run $other $changeflags sparse $testfile
log_must hasflag - $testfile
log_must user_run $owner rm $testfile

#
# SYSTEM
#
log_must touch $testfile
log_must $changeflags system $testfile
log_must hasflag system $testfile
log_must $changeflags 0 $testfile
log_must hasflag - $testfile
log_must rm $testfile
log_must user_run $owner touch $testfile
log_must user_run $owner $changeflags system $testfile
log_mustnot user_run $other $changeflags nosystem $testfile
log_must hasflag system $testfile
log_must user_run $owner $changeflags 0 $testfile
log_mustnot user_run $other $changeflags system $testfile
log_must hasflag - $testfile
log_must user_run $owner rm $testfile

log_pass "DOS mode flags function correctly"
