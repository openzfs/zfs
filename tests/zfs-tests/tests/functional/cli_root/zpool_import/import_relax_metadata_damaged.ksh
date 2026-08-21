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
# Copyright (c) 2026, TrueNAS.
#

. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.kshlib

#
# DESCRIPTION:
#	Damaged file meta-data should make a txg unusable, unless the import
#	is explicitly told to tolerate the non-critical meta-data errors.
#
# STRATEGY:
#	1. Create a pool with two files and remember their hashsums.
#	2. Corrupt every copy of the indirect block of one of them on disk.
#	3. Verify that a dry run reports the last txg as unusable.
#	4. Import the same txg with -M and verify that it succeeds, that the
#	   intact file is still readable and the damaged one is not.
#	5. Verify that the pool is fully operable, and that -M needs neither
#	   -F nor -X.
#
# NOTES:
#	The dry run above requests a txg explicitly, which implies an extreme
#	rewind.  A non-extreme import verifies only the blocks born within the
#	last few txgs, so whether it notices the damage would depend on how
#	many txgs the export happens to consume.
#

verify_runnable "global"

log_onexit cleanup

log_assert "Tolerate damaged file meta-data on import with -M."

typeset intact="/$TESTPOOL1/intact"
typeset damaged="/$TESTPOOL1/damaged"
typeset -i blocks=80

log_must zpool create $TESTPOOL1 $VDEV0

log_must dd if=/dev/urandom of=$intact bs=128k count=$blocks
log_must dd if=/dev/urandom of=$damaged bs=128k count=$blocks
log_must sync_pool $TESTPOOL1
typeset digest=$(xxh128digest $intact)

corrupt_blocks_at_level $damaged 1
typeset -i txg=$(get_last_txg_synced $TESTPOOL1)

log_must zpool export $TESTPOOL1

# An indirect block of a file is fatal for the txg by default.
log_mustnot zpool import -d $DEVICE_DIR -nFX -T $txg $TESTPOOL1

# The very same txg is usable once the caller accepts the losses.
log_must zpool import -d $DEVICE_DIR -M -T $txg $TESTPOOL1

if [[ "$(xxh128digest $intact)" != "$digest" ]]; then
	log_fail "The intact file lost its content"
fi
log_mustnot eval "dd if=$damaged of=/dev/null bs=128k count=$blocks" \
    "2>/dev/null"

# The pool itself has to remain fully operable.
log_must eval "zpool status $TESTPOOL1 | grep -q '$VDEV0.*ONLINE'"
log_must dd if=/dev/urandom of=/$TESTPOOL1/new bs=128k count=1
log_must sync_pool $TESTPOOL1
log_must zfs snapshot $TESTPOOL1@snap

log_must zpool export $TESTPOOL1

# The option does not depend on a rewind being requested.
log_must zpool import -d $DEVICE_DIR -M $TESTPOOL1

log_pass "Tolerate damaged file meta-data on import with -M."
