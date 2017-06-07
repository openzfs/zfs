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
# 	Turning a disk offline and back online during I/O completes.
#
# STRATEGY:
#	1. Create a mirror and start some random I/O
#	2. For each disk in the mirror, set it offline and online
#	3. Verify the integrity of the file system and the resilvering.
#

verify_runnable "global"

DISKLIST=$(get_disklist $TESTPOOL)

function cleanup
{
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

log_assert "Turning a disk offline and back online during I/O completes."

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

typeset child_pid=""

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

for disk in $DISKLIST; do
	#
	# Allow some common data to reach each side of the mirror.
	#
	$SLEEP 30

        i=0
        while [[ $i -lt $iters ]]; do
		log_must $ZPOOL offline $TESTPOOL $disk
		check_state $TESTPOOL $disk "offline"
                if [[ $? != 0 ]]; then
                        log_fail "$disk of $TESTPOOL is not offline."
                fi

                (( i = i + 1 ))
        done

	#
	# Sleep to allow the two sides to get out of sync
	#
	$SLEEP 60

        log_must $ZPOOL online $TESTPOOL $disk
        check_state $TESTPOOL $disk "online"
        if [[ $? != 0 ]]; then
                log_fail "$disk of $TESTPOOL did not match online state"
        fi
done

for wait_pid in $child_pids
do
	$KILL $wait_pid
done

typeset dir=$(get_device_dir $DISKS)
verify_filesys "$TESTPOOL" "$TESTPOOL/$TESTFS" "$dir"

log_pass
