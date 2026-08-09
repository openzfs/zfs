#! /bin/ksh -p
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
# Copyright (c) 2024, 2025, Klara Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Create a large number of entries in the DDT. Then remove all entries and
# check that the DDT zap was shrunk. Use zdb to check that the zap object
# contains only one leaf block using zdb.
#

verify_runnable "global"

log_assert "Create a large number of entries in the DDT. " \
	"Ensure DDT ZAP object shrank after removing entries."

# We set the dedup log txg interval to 1, to get a log flush every txg,
# effectively disabling the log. Without this it's hard to predict when
# entries appear in the DDT ZAP
log_must save_tunable DEDUP_LOG_TXG_MAX
log_must set_tunable32 DEDUP_LOG_TXG_MAX 1
log_must save_tunable DEDUP_LOG_FLUSH_ENTRIES_MIN
log_must set_tunable32 DEDUP_LOG_FLUSH_ENTRIES_MIN 100000

function cleanup
{
	if poolexists $TESTPOOL ; then
		destroy_pool $TESTPOOL
	fi
	log_must restore_tunable DEDUP_LOG_TXG_MAX
	log_must restore_tunable DEDUP_LOG_FLUSH_ENTRIES_MIN
}

log_onexit cleanup

log_must create_pool $TESTPOOL $DISKS
log_must zfs create -o dedup=sha256 -o recordsize=512 $TESTPOOL/$TESTFS
typeset mountpoint=$(get_prop mountpoint $TESTPOOL/$TESTFS)

log_must dd if=/dev/urandom of=$mountpoint/file bs=512k count=1
sync_pool $TESTPOOL

zap_obj=$(zdb -DDD $TESTPOOL | grep "DDT-sha256-zap-unique" | sed -n 's/.*object=//p')

nleafs=$(zdb -dddd $TESTPOOL "$zap_obj" | grep "Leaf blocks:" | awk -F\: '{print($2);}')
log_must test 1 -lt $nleafs

nleafs_old=$nleafs

log_must truncate -s 512 $mountpoint/file
sync_pool $TESTPOOL
nleafs=$(zdb -dddd $TESTPOOL "$zap_obj" | grep "Leaf blocks:" | awk -F\: '{print($2);}')
log_must test $nleafs -lt $nleafs_old

log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL

nleafs=$(zdb -dddd $TESTPOOL "$zap_obj" | grep "Leaf blocks:" | awk -F\: '{print($2);}')
log_must test $nleafs -lt $nleafs_old

log_must zdb -b $TESTPOOL

log_pass "ZAP object shrank after removing entries."
