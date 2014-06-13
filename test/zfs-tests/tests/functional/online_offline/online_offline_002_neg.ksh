#!/usr/bin/ksh -p
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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/online_offline/online_offline.cfg

#
# DESCRIPTION:
# 	Turning both disks offline should fail.
#
# STRATEGY:
#	1. Create a mirror and start some random I/O
#	2. For each disk in the mirror, set them offline sequentially.
#	3. Only one disk can be offline at any one time.
#	4. Verify the integrity of the file system and the resilvering.
#

verify_runnable "global"

DISKLIST=$(get_disklist $TESTPOOL)

function cleanup
{
	if [[ -n "$child_pids" ]]; then
		for wait_pid in $child_pids
		do
		        $KILL $wait_pid
		done
	fi

	#
	# Ensure we don't leave disks in the offline state
	#
	for disk in $DISKLIST; do
		log_must $ZPOOL online $TESTPOOL $disk
		check_state $TESTPOOL $disk "online"
		if [[ $? != 0 ]]; then
			log_fail "Unable to online $disk"
		fi

	done

	[[ -e $TESTDIR ]] && log_must $RM -rf $TESTDIR/*
}

log_assert "Turning both disks offline should fail."

options=""
options_display="default options"

log_onexit cleanup

[[ -n "$HOLES_FILESIZE" ]] && options=" $options -f $HOLES_FILESIZE "

[[ -n "$HOLES_BLKSIZE" ]] && options="$options -b $HOLES_BLKSIZE "

[[ -n "$HOLES_COUNT" ]] && options="$options -c $HOLES_COUNT "

[[ -n "$HOLES_SEED" ]] && options="$options -s $HOLES_SEED "

[[ -n "$HOLES_FILEOFFSET" ]] && options="$options -o $HOLES_FILEOFFSET "

options="$options -r "

[[ -n "$options" ]] && options_display=$options

child_pid=""

typeset -i iters=2
typeset -i index=0

i=0
while [[ $i -lt $iters ]]; do
	log_note "Invoking $FILE_TRUNC with: $options_display"
	$FILE_TRUNC $options $TESTDIR/$TESTFILE.$i &
	typeset pid=$!

	$SLEEP 1
	if ! $PS -p $pid > /dev/null 2>&1; then
		log_fail "$FILE_TRUNC $options $TESTDIR/$TESTFILE.$i"
	fi

	child_pids="$child_pids $pid"
	((i = i + 1))
done

set -A disk "" $DISKLIST

log_must $ZPOOL offline $TESTPOOL ${disk[1]}

$SLEEP 60

for wait_pid in $child_pids
do
	$KILL $wait_pid
done
child_pids=""

i=1
while [[ $i != ${#disk[*]} ]]; do
	log_must $ZPOOL online $TESTPOOL ${disk[$i]}
	check_state $TESTPOOL ${disk[$i]} "online"
	if [[ $? != 0 ]]; then
		log_fail "${disk[$i]} of $TESTPOOL did not match online state"
	fi

	((i = i + 1))
done

typeset dir=$(get_device_dir $DISKS)
verify_filesys "$TESTPOOL" "$TESTPOOL/$TESTFS" "$dir"

log_pass
