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
# Portions Copyright 2021 iXsystems, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
# Verify crtime is functional with xattr=on|sa

verify_runnable "both"

#
# The statx system call was first added in the 4.11 Linux kernel.  Prior to this
# change there was no mechanism to obtain birth time on Linux.  Therefore, this
# test is expected to fail on older kernels and is skipped.
#
if is_linux; then
	if [[ $(linux_version) -lt $(linux_version "4.11") ]]; then
		log_unsupported "Requires statx(2) system call on Linux"
	fi
	typeset stat_version=$(stat --version | awk '{ print $NF; exit }')
	if compare_version_gte "8.30" "${stat_version}"; then
		log_unsupported "Requires coreutils stat(1) > 8.30 on Linux"
	fi
fi

log_assert "Verify crtime is functional."

set -A args "sa" "on"
typeset TESTFILE=$TESTDIR/testfile

for arg in ${args[*]}; do
	log_note "Testing with xattr set to $arg"
	log_must zfs set xattr=$arg $TESTPOOL
	rm -f $TESTFILE
	log_must touch $TESTFILE
	typeset -i crtime=$(stat_crtime $TESTFILE)
	typeset -i ctime=$(stat_ctime $TESTFILE)
	if (( crtime != ctime )); then
		log_fail "Incorrect crtime ($crtime != $ctime)"
	fi
	log_must touch $TESTFILE
	typeset -i crtime1=$(stat_crtime $TESTFILE)
	if (( crtime1 != crtime )); then
		log_fail "touch modified crtime ($crtime1 != $crtime)"
	fi
done

log_pass "Verified crtime is functional."
