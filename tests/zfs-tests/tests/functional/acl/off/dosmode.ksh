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

#
# Portions Copyright 2021 iXsystems, Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/acl/acl_common.kshlib

#
# DESCRIPTION:
#	Verify that DOS mode flags function correctly.
#
#	These flags are not currently exposed on Linux, so the test is
#	only useful on FreeBSD.
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

	ls -lo $path | awk '{ gsub(",", "\n", $5); print $5 }' | grep -qxF $flag
}

log_assert "Verify DOS mode flags function correctly"
log_onexit cleanup

tests_base=$STF_SUITE/tests/functional/acl/off
testfile=$TESTDIR/testfile
owner=$ZFS_ACL_STAFF1
other=$ZFS_ACL_STAFF2

#
# ARCHIVE
#
# This flag is set by ZFS when a file has been updated to indicate that
# the file needs to be archived.
#
log_must touch $testfile
log_must hasflag uarch $testfile
log_must chflags nouarch $testfile
log_must hasflag - $testfile
log_must touch $testfile
log_must hasflag uarch $testfile
log_must rm $testfile
log_must user_run $owner touch $testfile
log_must hasflag uarch $testfile
log_must user_run $owner chflags nouarch $testfile
log_mustnot user_run $other chflags uarch $testfile
log_must hasflag - $testfile
log_must user_run $owner touch $testfile
log_mustnot user_run $other chflags nouarch $testfile
log_must hasflag uarch $testfile
log_must user_run $owner rm $testfile

#
# HIDDEN
#
log_must touch $testfile
log_must chflags hidden $testfile
log_must hasflag hidden $testfile
log_must chflags 0 $testfile
log_must hasflag - $testfile
log_must rm $testfile
log_must user_run $owner touch $testfile
log_must user_run $owner chflags hidden $testfile
log_mustnot user_run $other chflags nohidden $testfile
log_must hasflag hidden $testfile
log_must user_run $owner chflags 0 $testfile
log_mustnot user_run $other chflags hidden $testfile
log_must hasflag - $testfile
log_must user_run $owner rm $testfile


#
# OFFLINE
#
log_must touch $testfile
log_must chflags offline $testfile
log_must hasflag offline $testfile
log_must chflags 0 $testfile
log_must hasflag - $testfile
log_must rm $testfile
log_must user_run $owner touch $testfile
log_must user_run $owner chflags offline $testfile
log_mustnot user_run $other chflags nooffline $testfile
log_must hasflag offline $testfile
log_must user_run $owner chflags 0 $testfile
log_mustnot user_run $other chflags offline $testfile
log_must hasflag - $testfile
log_must user_run $owner rm $testfile

#
# READONLY
#
# This flag prevents users from writing or appending to the file,
# but root is always allowed the operation.
#
log_must touch $testfile
log_must chflags rdonly $testfile
log_must hasflag rdonly $testfile
log_must eval "echo 'root write allowed' >> $testfile"
log_must cat $testfile
log_must chflags 0 $testfile
log_must hasflag - $tesfile
log_must rm $testfile
# It is required to still be able to write to an fd that was opened RW before
# READONLY is set.  We have a special test program for that.
log_must user_run $owner touch $testfile
log_mustnot user_run $other chflags rdonly $testfile
log_must user_run $owner $tests_base/dosmode_readonly_write $testfile
log_mustnot user_run $other chflags nordonly $testfile
log_must hasflag rdonly $testfile
log_mustnot user_run $owner "echo 'user write forbidden' >> $testfile"
log_must eval "echo 'root write allowed' >> $testfile"
# We are still allowed to read and remove the file when READONLY is set.
log_must user_run $owner cat $testfile
log_must user_run $owner rm $testfile

#
# REPARSE
#
# FIXME: does not work, not sure if broken or testing wrong
#

#
# SPARSE
#
log_must truncate -s 1m $testfile
log_must chflags sparse $testfile
log_must hasflag sparse $testfile
log_must chflags 0 $testfile
log_must hasflag - $testfile
log_must rm $testfile
log_must user_run $owner truncate -s 1m $testfile
log_must user_run $owner chflags sparse $testfile
log_mustnot user_run $other chflags nosparse $testfile
log_must hasflag sparse $testfile
log_must user_run $owner chflags 0 $testfile
log_mustnot user_run $other chflags sparse $testfile
log_must hasflag - $testfile
log_must user_run $owner rm $testfile

#
# SYSTEM
#
log_must touch $testfile
log_must chflags system $testfile
log_must hasflag system $testfile
log_must chflags 0 $testfile
log_must hasflag - $testfile
log_must rm $testfile
log_must user_run $owner touch $testfile
log_must user_run $owner chflags system $testfile
log_mustnot user_run $other chflags nosystem $testfile
log_must hasflag system $testfile
log_must user_run $owner chflags 0 $testfile
log_mustnot user_run $other chflags system $testfile
log_must hasflag - $testfile
log_must user_run $owner rm $testfile

log_pass "DOS mode flags function correctly"
