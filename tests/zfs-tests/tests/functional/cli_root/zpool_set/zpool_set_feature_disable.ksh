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
# Copyright (c) 2026 Tigerblue77
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	"zpool set feature@<name>=disabled" moves an enabled feature back to
#	the disabled state, and refuses it while the feature is active or
#	while an enabled feature depends on it.  Whatever later needs a
#	disabled feature enables it again.
#
# STRATEGY:
#	1. Disable multi_vdev_crash_dump, check it reads "disabled" across an
#	   export and import and in the history, then enable it again.
#	2. Disable zstd_compress: "zfs set compression=zstd" must then fail.
#	   Enable it again: setting compression=zstd activates it at once,
#	   and disabling it then fails.
#	3. Set checksum=sha512 on a dataset while sha512 is unused, and
#	   disable sha512.  Writing to the dataset must enable and activate
#	   sha512 again, and the internal history must record the enable.
#	4. Disabling draid fails while draid_failure_domains is enabled.
#	   Disable both: adding a draid vdev must then fail.  Enabling
#	   draid_failure_domains enables draid too, and a draid vdev can then
#	   be added.  Disabling the now active draid fails, even once
#	   draid_failure_domains is disabled.
#	5. Disable raidz_expansion on a raidz pool: "zpool attach" to the raidz
#	   must then fail.  Enable it again, expand the raidz, and check that
#	   disabling the now active feature fails.
#	6. lz4_compress, active as soon as it is enabled, cannot be disabled.
#

verify_runnable "global"

function cleanup
{
	destroy_pool $TESTPOOL1
	rm -f $TEST_BASE_DIR/feature_disable.*
}

function check_prop # feature value
{
	typeset val=$(get_pool_prop feature@$1 $TESTPOOL1)

	[[ "$val" == "$2" ]] ||
	    log_fail "feature@$1 is '$val', expected '$2'"
}

log_assert "'zpool set feature@<name>=disabled' disables an unused feature"
log_onexit cleanup

for i in 0 1 2 3 4; do
	log_must truncate -s $MINVDEVSIZE $TEST_BASE_DIR/feature_disable.$i
done

# 1. A feature that nothing activates.
log_must zpool create -f $TESTPOOL1 $TEST_BASE_DIR/feature_disable.0
check_prop multi_vdev_crash_dump enabled
log_must zpool set feature@multi_vdev_crash_dump=disabled $TESTPOOL1
check_prop multi_vdev_crash_dump disabled
log_must eval "zpool history $TESTPOOL1 | \
    grep -q 'feature@multi_vdev_crash_dump=disabled'"
log_must zpool export $TESTPOOL1
log_must zpool import -d $TEST_BASE_DIR $TESTPOOL1
check_prop multi_vdev_crash_dump disabled
log_must zpool set feature@multi_vdev_crash_dump=enabled $TESTPOOL1
check_prop multi_vdev_crash_dump enabled

# 2. zstd_compress, which "zfs set compression=zstd" activates.
log_must zfs create $TESTPOOL1/zstd
log_must zpool set feature@zstd_compress=disabled $TESTPOOL1
check_prop zstd_compress disabled
log_mustnot zfs set compression=zstd $TESTPOOL1/zstd
log_must zpool set feature@zstd_compress=enabled $TESTPOOL1
log_must zfs set compression=zstd $TESTPOOL1/zstd
check_prop zstd_compress active
log_mustnot zpool set feature@zstd_compress=disabled $TESTPOOL1
check_prop zstd_compress active

# 3. sha512, which only the first block written with it activates.
log_must zfs create $TESTPOOL1/sha512
sync_pool $TESTPOOL1
log_must zfs set checksum=sha512 $TESTPOOL1/sha512
check_prop sha512 enabled
log_must zpool set feature@sha512=disabled $TESTPOOL1
check_prop sha512 disabled
log_must dd if=/dev/urandom of=/$TESTPOOL1/sha512/file bs=128k count=8
sync_pool $TESTPOOL1
check_prop sha512 active
# Only internal records: the command record of the "zpool set" above can
# be written after the enable, in the same txg.
log_must eval "zpool history -i $TESTPOOL1 | \
    grep '\[txg:[0-9]*\] set feature@sha512=' | tail -n 1 | \
    grep -q 'feature@sha512=enabled'"
log_must zpool export $TESTPOOL1
log_must zpool import -d $TEST_BASE_DIR $TESTPOOL1
check_prop sha512 active

# 4. draid and draid_failure_domains, which depends on it.
log_mustnot zpool set feature@draid=disabled $TESTPOOL1
check_prop draid enabled
log_must zpool set feature@draid_failure_domains=disabled $TESTPOOL1
log_must zpool set feature@draid=disabled $TESTPOOL1
check_prop draid disabled
check_prop draid_failure_domains disabled
log_mustnot zpool add -f $TESTPOOL1 draid1:2d:1s \
    $TEST_BASE_DIR/feature_disable.{1,2,3,4}
log_must zpool set feature@draid_failure_domains=enabled $TESTPOOL1
check_prop draid enabled
log_must zpool add -f $TESTPOOL1 draid1:2d:1s \
    $TEST_BASE_DIR/feature_disable.{1,2,3,4}
check_prop draid active
log_must zpool set feature@draid_failure_domains=disabled $TESTPOOL1
log_mustnot zpool set feature@draid=disabled $TESTPOOL1
check_prop draid active
log_must destroy_pool $TESTPOOL1

# 5. raidz_expansion.
log_must zpool create -f $TESTPOOL1 raidz1 \
    $TEST_BASE_DIR/feature_disable.{0,1,2}
log_must zpool set feature@raidz_expansion=disabled $TESTPOOL1
check_prop raidz_expansion disabled
log_mustnot zpool attach $TESTPOOL1 raidz1-0 $TEST_BASE_DIR/feature_disable.3
log_must zpool set feature@raidz_expansion=enabled $TESTPOOL1
log_must zpool attach -w $TESTPOOL1 raidz1-0 $TEST_BASE_DIR/feature_disable.3
check_prop raidz_expansion active
log_mustnot zpool set feature@raidz_expansion=disabled $TESTPOOL1
check_prop raidz_expansion active

# 6. A feature active as soon as it is enabled.
check_prop lz4_compress active
log_mustnot zpool set feature@lz4_compress=disabled $TESTPOOL1
check_prop lz4_compress active

log_pass "'zpool set feature@<name>=disabled' disables an unused feature"
