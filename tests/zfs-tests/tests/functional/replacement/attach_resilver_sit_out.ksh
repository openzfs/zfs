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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
# Copyright (c) 2025, Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/replacement/replacement.cfg

#
# DESCRIPTION:
# 	Attaching disks while a disk is sitting out reads should pass
#
# STRATEGY:
#	1. Create raidz pools
#	2. Make one disk slower and trigger a read sit out for that disk
#	3. Start some random I/O
#	4. Attach a disk to the pool.
#	5. Verify the integrity of the file system and the resilvering.

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
		for wait_pid in $child_pids; do
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

function attach_test
{
	typeset vdev=$1
	typeset disk=$2

	typeset i=0
	while [[ $i -lt $iters ]]; do
		log_note "Invoking file_trunc with: $options_display on $TESTFILE.$i"
		file_trunc $options $TESTDIR/$TESTFILE.$i &
		typeset pid=$!

		sleep 1

		child_pids="$child_pids $pid"
		((i = i + 1))
	done

	# attach disk with a slow drive still present
	SECONDS=0
	log_must zpool attach -w $TESTPOOL1 $vdev $disk
	log_note took $SECONDS seconds to attach disk

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
	truncate -s $DEVSIZE $TESTDIR/$TESTFILE1.$i
	specials_list="$specials_list $TESTDIR/$TESTFILE1.$i"

	((i = i + 1))
done

slow_disk=$TESTDIR/$TESTFILE1.3
log_must truncate -s $DEVSIZE $TESTDIR/$REPLACEFILE

# Test file size in MB
count=200

for type in "raidz1" "raidz2" "raidz3" ; do
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

	typeset top=$(zpool status -j | jq -r ".pools.$TESTPOOL1.vdevs[].vdevs[].name")
	attach_test $top $TESTDIR/$REPLACEFILE

	log_must eval "zpool iostat -v $TESTPOOL1 | grep \"$REPLACEFILE\""

	destroy_pool $TESTPOOL1
	log_must rm -rf /$TESTPOOL1
done

log_pass
