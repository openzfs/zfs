#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
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

#
# Copyright 2016 Nexenta Systems, Inc.
#

. $STF_SUITE/tests/functional/cli_root/zpool_labelclear/labelclear.cfg

# DESCRIPTION:
# Check that zpool labelclear will refuse to clear the label
# on ACTIVE vdevs of exported pool without -f, and will succeeded with -f.
#
# STRATEGY:
# 1. Create a pool with log device.
# 2. Export the pool.
# 3. Check that zpool labelclear returns non-zero when trying to
#    clear the label on ACTIVE vdevs, and succeeds with -f.
# 4. Add auxiliary vdevs (cache/spare).
# 5. Check that zpool labelclear succeeds on auxiliary vdevs of
#    exported pool.

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_onexit cleanup
log_assert "zpool labelclear will fail on ACTIVE vdevs of exported pool and" \
    "succeed with -f"

for vdevtype in "" "cache" "spare"; do
	# Create simple pool, skip any mounts
	log_must zpool create -O mountpoint=none -f $TESTPOOL $disk1 log $disk2
	# Add auxiliary vdevs (cache/spare)
	if [[ -n $vdevtype ]]; then
		log_must zpool add $TESTPOOL $vdevtype $disk3
	fi
	# Export the pool
	log_must zpool export $TESTPOOL

	# Check that labelclear will fail without -f
	log_mustnot zpool labelclear $disk1
	log_must zdb -lq $disk1
	log_mustnot zpool labelclear $disk2
	log_must zdb -lq $disk2

	# Check that labelclear will succeed with -f
	log_must zpool labelclear -f $disk1
	log_mustnot zdb -lq $disk1
	log_must zpool labelclear -f $disk2
	log_mustnot zdb -lq $disk2

	# Check that labelclear on auxiliary vdevs will succeed
	if [[ -n $vdevtype ]]; then
		log_must zpool labelclear $disk3
		log_mustnot zdb -lq $disk3
	fi
done

log_pass "zpool labelclear will fail on ACTIVE vdevs of exported pool and" \
    "succeed with -f"
