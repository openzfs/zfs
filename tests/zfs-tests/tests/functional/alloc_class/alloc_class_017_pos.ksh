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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2026, TrueNAS
#

. $STF_SUITE/tests/functional/alloc_class/alloc_class.kshlib

#
# DESCRIPTION:
#	The alloc_bias property is readable and settable on spare vdevs,
#	and persists across export/import.  A spare with a matching bias
#	can replace a vdev of that allocation class.
#
# STRATEGY:
#	1. Create a pool with a normal mirror, a single special vdev, and
#	   two spares.
#	2. Verify both spares default to alloc_bias=none.
#	3. Set alloc_bias=special on one spare; verify and persist.
#	4. Set alloc_bias=dedup; verify and persist.
#	5. Set alloc_bias=log; verify and persist.
#	6. Reset to alloc_bias=none; verify and persist.
#	7. Set alloc_bias=special and replace the faulted special vdev;
#	   verify the replacement succeeds.
#

verify_runnable "global"

claim="alloc_bias is settable on spare vdevs and persists across export/import"

log_assert $claim
log_onexit cleanup

log_must disk_setup

# Pool: normal mirror + single special vdev + two spares.
# CLASS_DISK2 will be the biased spare; CLASS_DISK3 the unbiased one.
log_must zpool create -f $TESTPOOL \
    mirror $ZPOOL_DISK0 $ZPOOL_DISK1 \
    special $CLASS_DISK0 \
    spare $CLASS_DISK2 $CLASS_DISK3

# Both spares should default to none.
for spare in $CLASS_DISK2 $CLASS_DISK3; do
	BIAS=$(zpool get -H -o value alloc_bias $TESTPOOL $spare)
	[[ "$BIAS" == "none" ]] ||
	    log_fail "spare $spare: expected none, got $BIAS"
done

# Cycle through biases on CLASS_DISK2, checking persistence each time.
for bias in special dedup log none; do
	log_must zpool set alloc_bias=$bias $TESTPOOL $CLASS_DISK2
	BIAS=$(zpool get -H -o value alloc_bias $TESTPOOL $CLASS_DISK2)
	[[ "$BIAS" == "$bias" ]] ||
	    log_fail "after set $bias: got $BIAS"

	log_must zpool export $TESTPOOL
	log_must zpool import -d $TEST_BASE_DIR $TESTPOOL
	BIAS=$(zpool get -H -o value alloc_bias $TESTPOOL $CLASS_DISK2)
	[[ "$BIAS" == "$bias" ]] ||
	    log_fail "after reimport with $bias: got $BIAS"
done

# Set CLASS_DISK2 to special and verify it can replace the special vdev.
log_must zpool set alloc_bias=special $TESTPOOL $CLASS_DISK2
log_must zpool replace $TESTPOOL $CLASS_DISK0 $CLASS_DISK2

log_must zpool destroy -f $TESTPOOL
log_pass $claim
