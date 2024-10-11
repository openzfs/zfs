#!/bin/ksh -p
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
# Copyright (c) 2023, Klara Inc.
#

# DESCRIPTION:
#	Verify that new entries are not added to the DDT when dedup_table_quota has
#	been exceeded.
#
# STRATEGY:
#	1. Create a pool with dedup=on
#	2. Set threshold for on-disk DDT via dedup_table_quota
#	3. Verify the threshold is exceeded after zpool sync
#	4. Verify no new entries are added after subsequent sync's
#	5. Remove all but one entry from DDT
#	6. Verify new entries are added to DDT
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/events/events_common.kshlib

verify_runnable "both"

log_assert "DDT quota is enforced"

MOUNTDIR="$TEST_BASE_DIR/dedup_mount"
FILEPATH="$MOUNTDIR/dedup_file"
VDEV_GENERAL="$TEST_BASE_DIR/vdevfile.general.$$"
VDEV_DEDUP="$TEST_BASE_DIR/vdevfile.dedup.$$"
POOL="dedup_pool"

save_tunable TXG_TIMEOUT

# we set the dedup log txg interval to 1, to get a log flush every txg,
# effectively disabling the log. without this it's hard to predict when and
# where things appear on-disk
log_must save_tunable DEDUP_LOG_TXG_MAX
log_must set_tunable32 DEDUP_LOG_TXG_MAX 1

function cleanup
{
	if poolexists $POOL ; then
		destroy_pool $POOL
	fi
	log_must rm -fd $VDEV_GENERAL $VDEV_DEDUP $MOUNTDIR
	log_must restore_tunable TXG_TIMEOUT
	log_must restore_tunable DEDUP_LOG_TXG_MAX
}


function do_clean
{
	log_must destroy_pool $POOL
	log_must rm -fd $VDEV_GENERAL $VDEV_DEDUP $MOUNTDIR
}

function do_setup
{
	log_must truncate -s 5G $VDEV_GENERAL
	# Use 'xattr=sa' to prevent selinux xattrs influencing our accounting
	log_must zpool create -o ashift=12 -f -O xattr=sa -m $MOUNTDIR $POOL $VDEV_GENERAL
	log_must zfs set dedup=on $POOL
	log_must set_tunable32 TXG_TIMEOUT 600
}

function dedup_table_size
{
	get_pool_prop dedup_table_size $POOL
}

function dedup_table_quota
{
	get_pool_prop dedup_table_quota $POOL
}

function ddt_entries
{
	typeset -i entries=$(zpool status -D $POOL | \
		grep "dedup: DDT entries" | awk '{print $4}')

	echo ${entries}
}

function ddt_add_entry
{
	count=$1
	offset=$2
	expand=$3

	if [ -z "$offset" ]; then
		offset=1
	fi

	for i in {$offset..$count}; do
		echo "$i" > $MOUNTDIR/dedup-$i.txt
	done
	log_must sync_pool $POOL

	log_note range $offset - $(( count + offset - 1 ))
	log_note ddt_add_entry got $(ddt_entries)
}

# Create 6000 entries over three syncs
function ddt_nolimit
{
	do_setup

	log_note base ddt entries is $(ddt_entries)

	ddt_add_entry 1
	ddt_add_entry 100
	ddt_add_entry 101 5000
	ddt_add_entry 5001 6000

	log_must test $(ddt_entries) -eq 6000

	do_clean
}

function ddt_limit
{
	do_setup

	log_note base ddt entries is $(ddt_entries)

	log_must zpool set dedup_table_quota=32768 $POOL
	ddt_add_entry 100

	# it's possible to exceed dedup_table_quota over a single transaction,
	# ensure that the threshold has been exceeded
	cursize=$(dedup_table_size)
	log_must test $cursize -gt $(dedup_table_quota)

	# count the entries we have
	log_must test $(ddt_entries) -ge 100

	# attempt to add new entries
	ddt_add_entry 101 200
	log_must test $(ddt_entries) -eq 100
	log_must test $cursize -eq $(dedup_table_size)

	# remove all but one entry
	for i in {2..100}; do
		rm $MOUNTDIR/dedup-$i.txt
	done
	log_must sync_pool $POOL

	log_must test $(ddt_entries) -eq 1
	log_must test $cursize -gt $(dedup_table_size)
	cursize=$(dedup_table_size)

	log_must zpool set dedup_table_quota=none $POOL

	# create more entries
	zpool status -D $POOL
	ddt_add_entry 101 200
	log_must sync_pool $POOL

	log_must test $(ddt_entries) -eq 101
	log_must test $cursize -lt $(dedup_table_size)

	do_clean
}

function ddt_dedup_vdev_limit
{
	do_setup

	# add a dedicated dedup/special VDEV and enable an automatic quota
	if (( RANDOM % 2 == 0 )) ; then
		class="special"
	else
		class="dedup"
	fi
	log_must truncate -s 200M $VDEV_DEDUP
	log_must zpool add $POOL $class $VDEV_DEDUP
	log_must zpool set dedup_table_quota=auto $POOL

	log_must zfs set recordsize=1K $POOL
	log_must zfs set compression=zstd $POOL

	# Generate a working set to fill up the dedup/special allocation class
	log_must fio --directory=$MOUNTDIR --name=dedup-filler-1 \
		--rw=read --bs=1m --numjobs=2 --iodepth=8 \
		--size=512M --end_fsync=1 --ioengine=posixaio --runtime=1 \
		--group_reporting --fallocate=none --output-format=terse \
		--dedupe_percentage=0
	log_must sync_pool $POOL

	zpool status -D $POOL
	zpool list -v $POOL
	echo DDT size $(dedup_table_size), with $(ddt_entries) entries

	#
	# With no DDT quota in place, the above workload will produce over
	# 800,000 entries by using space in the normal class. With a quota, it
	# should be well under 500,000. However, logged entries are hard to
	# account for because they can appear on both logs, and can also
	# represent an eventual removal. This isn't easily visible from
	# outside, and even internally can result in going slightly over quota.
	# For here, we just set the entry count a little higher than what we
	# expect to allow for some instability.
	#
	log_must test $(ddt_entries) -le 650000

	do_clean
}

log_onexit cleanup

ddt_limit
ddt_nolimit
ddt_dedup_vdev_limit

log_pass "DDT quota is enforced"
