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
#	3. Checkpoint the pool, so that the blocks of the txg below are
#	   guaranteed to stay on disk, and note the last synced txg.
#	4. Verify that a dry run of that txg rewinds past it, and that the
#	   same dry run with -M accepts it.
#	5. Import that txg with -M and verify that it succeeds, that the
#	   intact file is still readable and the damaged one is not.
#	6. Verify that the pool is fully operable, and that -M needs neither
#	   -F nor -X.
#
# NOTES:
#	The dry runs above request a txg explicitly, which implies an extreme
#	rewind.  A non-extreme import verifies only the blocks born within the
#	last few txgs, and the export itself consumes exactly that many, so it
#	would never look at the damaged block.
#
#	Those same txgs consumed by the export are also enough for the blocks
#	the requested txg references to be freed and reallocated.  Without the
#	checkpoint holding them the txg may be unloadable for reasons having
#	nothing to do with the damage injected here, which is exactly what -M
#	is not supposed to tolerate.
#
#	A dry run reports an error whether it found a usable txg or not, so
#	only the txg it reports tells the two apart.
#

verify_runnable "global"

function custom_cleanup
{
	log_pos restore_tunable TXG_TIMEOUT
	cleanup
}

log_onexit custom_cleanup

log_assert "Tolerate damaged file meta-data on import with -M."

typeset intact="/$TESTPOOL1/intact"
typeset damaged="/$TESTPOOL1/damaged"
typeset -i blocks=8

log_must zpool create $TESTPOOL1 $VDEV0

log_must dd if=/dev/urandom of=$intact bs=128k count=$blocks
log_must dd if=/dev/urandom of=$damaged bs=128k count=$blocks
log_must sync_pool $TESTPOOL1
typeset digest=$(xxh128digest $intact)

corrupt_blocks_at_level $damaged 1

#
# From now on only the checkpoint below advances the txg, so the state it
# protects is the very state the imports below request.
#
log_must save_tunable TXG_TIMEOUT
log_must set_tunable32 TXG_TIMEOUT 5000

log_must zpool checkpoint $TESTPOOL1
typeset -i txg=$(get_last_txg_synced $TESTPOOL1)

log_must zpool export $TESTPOOL1

#
# An indirect block of a file is fatal for the txg by default.  A dry run
# of an explicitly requested txg never imports the pool, so the reported
# rewind target is what tells us that the txg itself was rejected: it is
# some older txg rather than the requested one.
#
typeset out
out=$(zpool import -d $DEVICE_DIR -nFX -T $txg $TESTPOOL1 2>&1) &&
    log_fail "Loading the damaged txg $txg unexpectedly succeeded"
log_note "$out"
echo "$out" | grep -q "(txg $txg)" &&
    log_fail "The damaged txg $txg was not rejected"

# The very same txg is a valid target once the caller accepts the losses.
out=$(zpool import -d $DEVICE_DIR -nFX -M -T $txg $TESTPOOL1 2>&1)
log_note "$out"
echo "$out" | grep -q "(txg $txg)" ||
    log_fail "The damaged txg $txg was not accepted with -M"

# And it can be imported for real, losing only the damaged file.
log_must zpool import -d $DEVICE_DIR -M -T $txg $TESTPOOL1
log_must zpool checkpoint -d $TESTPOOL1

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
