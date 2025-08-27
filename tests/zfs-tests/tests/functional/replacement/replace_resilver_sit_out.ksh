#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
# Copyright (c) 2025, Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/replacement/replacement.cfg

#
# DESCRIPTION:
# 	Replacing disks while a disk is sitting out reads should pass
#
# STRATEGY:
#	1. Create raidz and draid pools
#	2. Make one disk slower and trigger a read sit out for that disk
#	3. Start some random I/O
#	4. Replace a disk in the pool with another disk.
#	5. Verify the integrity of the file system and the resilvering.
#

verify_runnable "global"

save_tunable READ_SIT_OUT_SECS
set_tunable32 READ_SIT_OUT_SECS 120
save_tunable SIT_OUT_CHECK_INTERVAL
set_tunable64 SIT_OUT_CHECK_INTERVAL 20

function cleanup
{
	restore_tunable READ_SIT_OUT_SECS
	restore_tunable SIT_OUT_CHECK_INTERVAL
	log_must zinject -c all
	log_must zpool events -c

	if [[ -n "$child_pids" ]]; then
		for wait_pid in $child_pids
		do
			kill $wait_pid
		done
	fi

	if poolexists $TESTPOOL1; then
		destroy_pool $TESTPOOL1
	fi

	[[ -e $TESTDIR ]] && log_must rm -rf $TESTDIR/*
}

log_assert "Replacing a disk during I/O with a sit out completes."

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

child_pids=""

function replace_test
{
	typeset -i iters=2
	typeset disk1=$1
	typeset disk2=$2
	typeset repl_type=$3

	typeset i=0
	while [[ $i -lt $iters ]]; do
		log_note "Invoking file_trunc with: $options_display on $TESTFILE.$i"
		file_trunc $options $TESTDIR/$TESTFILE.$i &
		typeset pid=$!

		sleep 1

		child_pids="$child_pids $pid"
		((i = i + 1))
	done

	typeset repl_flag="-w"
	if [[ "$repl_type" == "seq" ]]; then
		repl_flag="-ws"
	fi
	# replace disk with a slow drive still present
	SECONDS=0
	log_must zpool replace $repl_flag $TESTPOOL1 $disk1 $disk2
	log_note took $SECONDS seconds to replace disk

	for wait_pid in $child_pids
	do
		kill $wait_pid
	done
	child_pids=""

	log_must zinject -c all
	log_must zpool export $TESTPOOL1
	log_must zpool import -d $TESTDIR $TESTPOOL1
	log_must zfs umount $TESTPOOL1/$TESTFS1
	log_must zdb -cdui $TESTPOOL1/$TESTFS1
	log_must zfs mount $TESTPOOL1/$TESTFS1
	verify_pool $TESTPOOL1
}

DEVSIZE="150M"
specials_list=""
i=0
while [[ $i != 10 ]]; do
	log_must truncate -s $DEVSIZE $TESTDIR/$TESTFILE1.$i
	specials_list="$specials_list $TESTDIR/$TESTFILE1.$i"

	((i = i + 1))
done

slow_disk=$TESTDIR/$TESTFILE1.3
log_must truncate -s $DEVSIZE $TESTDIR/$REPLACEFILE

# Test file size in MB
count=400

for type in "raidz2" "raidz3" "draid2"; do
	create_pool $TESTPOOL1 $type $specials_list
	log_must zpool set autosit=on $TESTPOOL1 "${type}-0"
	log_must zfs create -o primarycache=none -o recordsize=512K \
	    $TESTPOOL1/$TESTFS1
	log_must zfs set mountpoint=$TESTDIR1 $TESTPOOL1/$TESTFS1

	log_must dd if=/dev/urandom of=/$TESTDIR1/bigfile bs=1M count=$count

	# Make one disk 100ms slower to trigger a sit out
	log_must zinject -d $slow_disk -D100:1 -T read $TESTPOOL1

	# Do some reads and wait for sit out on slow disk
	SECONDS=0
	typeset -i size=0
	for i in $(seq 1 $count) ; do
		dd if=/$TESTDIR1/bigfile skip=$i bs=1M count=1 of=/dev/null
		size=$i

		sit_out=$(get_vdev_prop sit_out $TESTPOOL1 $slow_disk)
		if [[ "$sit_out" == "on" ]] ; then
			break
		fi
	done
	log_must test "$(get_vdev_prop sit_out $TESTPOOL1 $slow_disk)" == "on"
	log_note took $SECONDS seconds to reach sit out reading ${size}M
	log_must zpool status -s $TESTPOOL1

	typeset repl_type="replace"
	if [[ "$type" == "draid2" && $((RANDOM % 2)) -eq 0 ]]; then
		repl_type="seq"
	fi
	replace_test $TESTDIR/$TESTFILE1.1 $TESTDIR/$REPLACEFILE $repl_type

	log_must eval "zpool iostat -v $TESTPOOL1 | grep \"$REPLACEFILE\""

	destroy_pool $TESTPOOL1
	log_must rm -rf /$TESTPOOL1
done

log_pass
