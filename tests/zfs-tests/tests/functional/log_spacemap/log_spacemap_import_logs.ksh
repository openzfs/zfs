#! /bin/ksh -p
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2019 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Log spacemaps are generally destroyed at export in order to
# not induce performance overheads at import time. As a result,
# the log spacemap codepaths that read the logs in import times
# are not tested outside of ztest and pools with DEBUG bits doing
# many imports/exports while running the test suite.
#
# This test uses an internal tunable and forces ZFS to keep the
# log spacemaps at export, and then re-imports the pool, thus
# providing explicit testing of those codepaths. It also uses
# another tunable to load all the metaslabs when the pool is
# re-imported so more assertions and verifications will be hit.
#
# STRATEGY:
#	1. Create pool.
#	2. Do a couple of writes to generate some data for spacemap logs.
#	3. Set tunable to keep logs after export.
#	4. Export pool and verify that there are logs with zdb.
#	5. Set tunable to load all metaslabs at import.
#	6. Import pool.
#	7. Reset tunables.
#

verify_runnable "global"

function cleanup
{
	log_must set_tunable64 KEEP_LOG_SPACEMAPS_AT_EXPORT 0
	log_must set_tunable64 METASLAB_DEBUG_LOAD 0
	if poolexists $LOGSM_POOL; then
		log_must zpool destroy -f $LOGSM_POOL
	fi
}
log_onexit cleanup

LOGSM_POOL="logsm_import"
TESTDISK="$(echo $DISKS | cut -d' ' -f1)"

log_must zpool create -o cachefile=none -f $LOGSM_POOL $TESTDISK
log_must zfs create $LOGSM_POOL/fs

log_must dd if=/dev/urandom of=/$LOGSM_POOL/fs/00 bs=128k count=10
sync_all_pools
log_must dd if=/dev/urandom of=/$LOGSM_POOL/fs/00 bs=128k count=10
sync_all_pools

log_must set_tunable64 KEEP_LOG_SPACEMAPS_AT_EXPORT 1
log_must zpool export $LOGSM_POOL

LOGSM_COUNT=$(zdb -m -e $LOGSM_POOL | grep "Log Spacemap object" | wc -l)
if (( LOGSM_COUNT == 0 )); then
	log_fail "Pool does not have any log spacemaps after being exported"
fi

log_must set_tunable64 METASLAB_DEBUG_LOAD 1
log_must zpool import $LOGSM_POOL

log_pass "Log spacemaps imported with no errors"
