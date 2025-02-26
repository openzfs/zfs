#!/bin/ksh -p
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
# Copyright (c) 2025, Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify pool's status_json kstat output.
#
# STRATEGY:
# 1. Compare kstat output with zpool status -jp output.
#

log_assert "Verify pool's status_json kstat output."

mkdir -p $TESTDIR
truncate -s 80M $TESTDIR/file{1..28}
DISK=${DISKS%% *}

# Create complex pool configs to exercise JSON
zpool create -f tp1 draid $TESTDIR/file{1..10} \
	special $DISK \
	dedup $TESTDIR/file11 \
	special $TESTDIR/file12 \
	cache $TESTDIR/file13 \
	log $TESTDIR/file14
zpool create -f tp2 mirror $TESTDIR/file{15,16} \
	raidz1 $TESTDIR/file{17,18,19} \
	cache $TESTDIR/file20 \
	log $TESTDIR/file21 \
	special mirror $TESTDIR/file{22,23} \
	dedup mirror $TESTDIR/file{24,25} \
	spare $TESTDIR/file{26,27,28}

function cleanup
{
	zpool destroy tp1
	zpool destroy tp2

	rm $TESTDIR/file{1..28}
	rmdir $TESTDIR
}

log_onexit cleanup

function sanity_check
{
	log_must eval 'kstat_pool tp1 status_json | jq >/dev/null'
	log_must eval 'kstat_pool tp2 status_json | jq >/dev/null'
}

TESTPOOL=tp0

# JSON from userland using zpool status
function ujq
{
	zpool status -jp $TESTPOOL | jq "$*"
}
function ujqe
{
	zpool status -jp $TESTPOOL | jq -e "$*" >/dev/null
}

# JSON from kernel using kstat
function kjq
{
	kstat_pool $TESTPOOL status_json | jq "$*"
}
function kjqe
{
	kstat_pool $TESTPOOL status_json | jq -e "$*" >/dev/null
}

function verify_testpool1
{
	TESTPOOL=tp1

	log_must ujqe '.pools | has("tp1")'
	log_must kjqe '.pools | has("tp1")'

	json=".pools.tp1 | {name, state, pool_guid, spa_version, zpl_version, error_count}"
	log_must diff -u <(ujq "$json") <(kjq "$json")
	log_must kjqe '.pools.tp1 | has("txg")'

	log_must ujqe '.pools.tp1 | has("vdevs")'
	log_must kjqe '.pools.tp1 | has("vdevs")'

	# root vdev {
	log_must ujqe '.pools.tp1.vdevs | has("tp1")'
	log_must kjqe '.pools.tp1.vdevs | has("tp1")'

	json=".pools.tp1.vdevs.tp1 | {name, vdev_type, guid, class, state, total_space, def_space, read_erros, write_errors, checksum_errors}"
	log_must diff -u <(ujq "$json") <(kjq "$json")
	log_must kjqe '.pools.tp1.vdevs.tp1 | has("alloc_space")'
	# }

	# draid vdev {
	log_must ujqe '.pools.tp1.vdevs.tp1 | has("vdevs")'
	log_must kjqe '.pools.tp1.vdevs.tp1 | has("vdevs")'

	log_must ujqe '.pools.tp1.vdevs.tp1.vdevs | has("draid1:8d:10c:0s-0")'
	log_must kjqe '.pools.tp1.vdevs.tp1.vdevs | has("draid1:8d:10c:0s-0")'

	json='.pools.tp1.vdevs.tp1.vdevs["draid1:8d:10c:0s-0"] | {name, total_space, def_space, rep_dev_size, read_errors, write_errors, checksum_errors}'
	log_must diff -u <(ujq "$json") <(kjq "$json")
	log_must diff -u <(echo \"draid\") <(kjq '.pools.tp1.vdevs.tp1.vdevs["draid1:8d:10c:0s-0"].vdev_type')
	log_must diff -u <(echo \"normal\") <(kjq '.pools.tp1.vdevs.tp1.vdevs["draid1:8d:10c:0s-0"].class')
	log_must diff -u <(echo \"ONLINE\") <(kjq '.pools.tp1.vdevs.tp1.vdevs["draid1:8d:10c:0s-0"].state')
	log_must kjqe '.pools.tp1.vdevs.tp1.vdevs["draid1:8d:10c:0s-0"] | has("guid")'

	json='.pools.tp1.vdevs.tp1.vdevs["draid1:8d:10c:0s-0"].vdevs'
	log_must diff -u <(ujq "$json") <(kjq "$json")
	# }

	# dedup {
	log_must ujqe '.pools.tp1 | has("dedup")'
	log_must kjqe '.pools.tp1 | has("dedup")'

	json='.pools.tp1.dedup'
	log_must diff -u <(ujq "$json") <(kjq "$json")
	# }

	# special {
	log_must ujqe '.pools.tp1 | has("special")'
	log_must kjqe '.pools.tp1 | has("special")'

	ujson='.pools.tp1.special.loop5         | del(.name)'
	kjson='.pools.tp1.special["/dev/loop5"] | del(.name)'
	log_must diff -u <(ujq "$ujson") <(kjq "$kjson")

	json='.pools.tp1.special["/var/tmp/testdir/file12"]'
	log_must diff -u <(ujq "$json") <(kjq "$json")
	# }

	# logs {
	log_must ujqe '.pools.tp1 | has("logs")'
	log_must kjqe '.pools.tp1 | has("logs")'

	json='.pools.tp1.logs'
	log_must diff -u <(ujq "$json") <(kjq "$json")
	# }

	# l2cache {
	log_must ujqe '.pools.tp1 | has("l2cache")'
	log_must kjqe '.pools.tp1 | has("l2cache")'

	json='.pools.tp1.l2cache'
	log_must diff -u <(ujq "$json") <(kjq "$json")
	# }
}

function verify_testpool2
{
	TESTPOOL=tp2

	log_must ujqe '.pools | has("tp2")'
	log_must kjqe '.pools | has("tp2")'

	json=".pools.tp2 | {name, state, pool_guid, spa_version, zpl_version, error_count}"
	log_must diff -u <(ujq "$json") <(kjq "$json")
	log_must kjqe '.pools.tp2 | has("txg")'

	log_must ujqe '.pools.tp2 | has("vdevs")'
	log_must kjqe '.pools.tp2 | has("vdevs")'

	# root vdev {
	log_must ujqe '.pools.tp2.vdevs | has("tp2")'
	log_must kjqe '.pools.tp2.vdevs | has("tp2")'

	json=".pools.tp2.vdevs.tp2 | {name, vdev_type, guid, class, state, total_space, def_space, read_erros, write_errors, checksum_errors}"
	log_must diff -u <(ujq "$json") <(kjq "$json")
	log_must kjqe '.pools.tp2.vdevs.tp2 | has("alloc_space")'
	# }

	# mirror-0 vdev {
	log_must ujqe '.pools.tp2.vdevs.tp2 | has("vdevs")'
	log_must kjqe '.pools.tp2.vdevs.tp2 | has("vdevs")'

	log_must ujqe '.pools.tp2.vdevs.tp2.vdevs | has("mirror-0")'
	log_must kjqe '.pools.tp2.vdevs.tp2.vdevs | has("mirror-0")'

	json='.pools.tp2.vdevs.tp2.vdevs["mirror-0"] | {name, vdev_type, class, state, guid, total_space, def_space, rep_dev_size, read_errors, write_errors, checksum_errors}'
	log_must diff -u <(ujq "$json") <(kjq "$json")

	json='.pools.tp2.vdevs.tp2.vdevs["mirror-0"].vdevs'
	log_must diff -u <(ujq "$json") <(kjq "$json")
	# }

	# dedup {
	log_must ujqe '.pools.tp2 | has("dedup")'
	log_must kjqe '.pools.tp2 | has("dedup")'

	json='.pools.tp2.dedup'
	log_must diff -u <(ujq "$json") <(kjq "$json")
	# }

	# special {
	log_must ujqe '.pools.tp2 | has("special")'
	log_must kjqe '.pools.tp2 | has("special")'

	json='.pools.tp2.special'
	log_must diff -u <(ujq "$json") <(kjq "$json")
	# }

	# logs {
	log_must ujqe '.pools.tp2 | has("logs")'
	log_must kjqe '.pools.tp2 | has("logs")'

	json='.pools.tp2.logs'
	log_must diff -u <(ujq "$json") <(kjq "$json")
	# }

	# l2cache {
	log_must ujqe '.pools.tp2 | has("l2cache")'
	log_must kjqe '.pools.tp2 | has("l2cache")'

	json='.pools.tp2.l2cache'
	log_must diff -u <(ujq "$json") <(kjq "$json")
	# }

	# spares {
	log_must ujqe '.pools.tp2 | has("spares")'
	log_must kjqe '.pools.tp2 | has("spares")'

	json='.pools.tp2.spares'
	log_must diff -u <(ujq "$json") <(kjq "$json")
	# }
}

log_must sanity_check
log_must verify_testpool1
log_must verify_testpool2

log_pass "Pool's status_json kstat provides expected output."
