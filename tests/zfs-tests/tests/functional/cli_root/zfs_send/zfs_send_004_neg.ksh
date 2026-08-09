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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/cli_common.kshlib

#
# DESCRIPTION:
#	Verify 'zfs send' fails with malformed parameters.
#
# STRATEGY:
#	1. Define malformed parameters in array
#	2. Feed the parameters to 'zfs send'
#	3. Verify the result
#

verify_runnable "both"

function cleanup
{
	typeset snap f

	for snap in $snap1 $snap2 $snap3; do
		snapexists $snap && destroy_dataset $snap -f
	done

	for f in $tmpfile1 $tmpfile2; do
		if [[ -e $f ]]; then
			rm -f $f
		fi
	done
}

fs=$TESTPOOL/$TESTFS
snap1=$fs@snap1
snap2=$fs@snap2
snap3=$fs@snap3

set -A badargs \
	"" "$TESTPOOL" "$TESTFS" "$fs" "$fs@nonexistent_snap" "?" \
	"$snap1/blah" "$snap1@blah" "-i" "-x" "-i $fs" \
	"-x $snap1 $snap2" "-i $snap1" \
	"-i $snap2 $snap1" "$snap1 $snap2" "-i $snap1 $snap2 $snap3" \
	"-ii $snap1 $snap2" "-iii $snap1 $snap2" " -i $snap2 $snap1/blah" \
	"-i $snap2/blah $snap1" \
	"-i $snap2/blah $snap1/blah" \
	"-i $snap1 blah@blah" \
	"-i blah@blah $snap1" \
	"-i $snap1 ${snap2##*@}" "-i $snap1 @${snap2##*@}" \
	"-i ${snap1##*@} ${snap2##*@}" "-i @${snap1##*@} @${snap2##*@}" \
	"-i ${snap1##*@} $snap2/blah" "-i @${snap1##*@} $snap2/blah" \
	"-i @@${snap1##*@} $snap2" "-i $snap1 -i $snap1 $snap2" \
	"-i snap1 snap2" "-i $snap1 snap2" \
	"-i $snap1 $snap2 -i $snap1 $snap2" \
	"-i snap1 $snap2 -i snap1 $snap2"

log_assert "Verify that invalid parameters to 'zfs send' are caught."
log_onexit cleanup

log_must zfs snapshot $snap1
tmpfile1=$TESTDIR/testfile1.$$
log_must touch $tmpfile1
log_must zfs snapshot $snap2
tmpfile2=$TESTDIR/testfile2.$$
log_must touch $tmpfile2
log_must zfs snapshot $snap3

typeset -i i=0
while (( i < ${#badargs[*]} ))
do
	log_mustnot eval "zfs send ${badargs[i]} > /dev/null"

	(( i = i + 1 ))
done

#Testing zfs send fails by send backup stream to terminal
for arg in "$snap1" "-i $snap1 $snap2"; do
	log_mustnot eval "zfs send $arg >/dev/console"
done

log_pass "Invalid parameters to 'zfs send' are caught as expected."
