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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
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
#	1. Create a multidisk stripe and start some random I/O
#	2. For two disks in the stripe, set them offline sequentially.
#	3. Zpool offline should fail in both cases.
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

	if poolexists $TESTPOOL1; then
		destroy_pool $TESTPOOL1
	fi

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

child_pid=""

typeset -i iters=2
typeset -i index=0

specials_list=""
i=0
while [[ $i != 3 ]]; do
	$MKFILE 100m $TESTDIR/$TESTFILE1.$i
	specials_list="$specials_list $TESTDIR/$TESTFILE1.$i"

	((i = i + 1))
done

create_pool $TESTPOOL1 $specials_list
log_must $ZFS create $TESTPOOL1/$TESTFS1
log_must $ZFS set mountpoint=$TESTDIR1 $TESTPOOL1/$TESTFS1

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

set -A disk "" $specials_list

log_mustnot $ZPOOL offline $TESTPOOL1 ${disk[1]}
log_mustnot $ZPOOL offline $TESTPOOL1 ${disk[2]}

$SLEEP 60

for wait_pid in $child_pids
do
	$KILL $wait_pid
done
child_pids=""

log_pass
