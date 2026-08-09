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
