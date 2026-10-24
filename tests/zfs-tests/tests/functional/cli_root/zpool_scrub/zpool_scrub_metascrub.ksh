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
# Copyright (c) 2026 Klara Inc.
#
# This software was developed by Mariusz Zaborski <oshogbo@FreeBSD.org>
# under sponsorship from Wasabi Technology, Inc. and Klara Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_scrub/zpool_scrub.cfg

#
# DESCRIPTION:
#	Verify that `zpool scrub -m` (metadata scrub) only detects metadata
#	errors and skips data block errors.
#
# STRATEGY:
#	1. Create a large file in the pool.
#	2. Inject data (L0) errors and run a metadata scrub.
#	3. Verify no errors are found (data blocks are skipped).
#	4. Inject indirect block (L1 and L2) errors, run a metadata
#	   scrub, and verify errors are detected.
#	5. Create a directory with many entries, inject errors into its
#	   L0 blocks (ZAP content, which is metadata), run a metadata
#	   scrub, and verify errors are detected.
#	6. Verify that last_scrubbed_txg is not updated by metascrub.
#	7. Verify that 'zpool status' retains a completed full scrub's
#	   summary line after a subsequent metascrub, and vice versa.
#	8. Verify that a metascrub does not discard data errors found by
#	   a previous full scrub from the persistent error log, and that
#	   a subsequent clean full scrub does.
#

verify_runnable "global"

function cleanup
{
	log_must zinject -c all
	rm -rf $TESTDIR/metascrub_file $TESTDIR/metascrub_dir \
	    $TESTDIR/metascrub_errfile
}

#
# Run a metadata scrub and verify that errors were detected.
#
function metascrub_inject_verify
{
	typeset inject_args="$@"

	log_note "Testing metadata injection: zinject $inject_args"
	log_must eval "zinject $inject_args"
	log_must zpool scrub -w -m $TESTPOOL
	log_mustnot check_pool_status $TESTPOOL "scan" "with 0 errors"
	log_must zinject -c all
	log_must zpool clear $TESTPOOL
}

log_onexit cleanup

log_assert "Verify that 'zpool scrub -m' only scrubs metadata blocks."

# Create a test file large enough to have L2 indirect blocks.
log_must file_write -o create -f "$TESTDIR/metascrub_file" \
    -b 1048576 -c 256 -d R
sync_pool $TESTPOOL

# Step 1-3: Inject data errors and run metascrub.
# Metascrub should NOT detect data errors since it skips data I/O.
log_must zinject -t data -e checksum -f 100 $TESTDIR/metascrub_file
log_must zpool scrub -w -m $TESTPOOL
log_must check_pool_status $TESTPOOL "scan" "with 0 errors"
log_must zinject -c all
log_must zpool clear $TESTPOOL

# Step 4: Inject indirect block errors and run metascrub.
# Metascrub SHOULD detect errors.
metascrub_inject_verify -t data -l 1 -e checksum -f 100 \
    $TESTDIR/metascrub_file
metascrub_inject_verify -t data -l 2 -e checksum -f 100 \
    $TESTDIR/metascrub_file

# Step 5: Inject errors into a directory's L0 blocks.
# Metascrub SHOULD detect errors.
log_must mkdir $TESTDIR/metascrub_dir
for i in $(seq 1 1000); do
	log_must touch $TESTDIR/metascrub_dir/file_$i
done
sync_pool $TESTPOOL
metascrub_inject_verify -t data -e checksum -f 100 \
    $TESTDIR/metascrub_dir

# Step 6: Verify that metascrub does not update last_scrubbed_txg.
typeset txg_before=$(zpool get -H -o value last_scrubbed_txg $TESTPOOL)
log_must zpool scrub -w -m $TESTPOOL
typeset txg_after=$(zpool get -H -o value last_scrubbed_txg $TESTPOOL)
log_must [ "$txg_before" = "$txg_after" ]

# Step 7: 'zpool status' should retain summaries for the last completed
# full scrub and the last completed metascrub independently. A run of
# one type must not erase the other type's summary line.

# Run a full scrub and confirm its summary appears.
log_must zpool scrub -w $TESTPOOL
log_must eval "zpool status $TESTPOOL | grep -q 'scan: scrub repaired'"

# Now run a metascrub. The full-scrub summary must still be present
# alongside a fresh metadata-scrub summary.
log_must zpool scrub -w -m $TESTPOOL
log_must eval "zpool status $TESTPOOL | grep -q 'scan: scrub repaired'"
log_must eval \
    "zpool status $TESTPOOL | grep -q 'scan: metadata scrub repaired'"

# Run another full scrub. The metascrub summary should still be retained.
log_must zpool scrub -w $TESTPOOL
log_must eval "zpool status $TESTPOOL | grep -q 'scan: scrub repaired'"
log_must eval \
    "zpool status $TESTPOOL | grep -q 'scan: metadata scrub repaired'"

# Step 8: A metascrub must not replace the existing error log, since it
# does not verify data blocks.
log_must file_write -o create -f "$TESTDIR/metascrub_errfile" \
    -b 1048576 -c 10 -d R
sync_pool $TESTPOOL
log_must zinject -a -t data -e io -T read $TESTDIR/metascrub_errfile
log_must zpool scrub -w $TESTPOOL
log_must zinject -c all
log_must eval "zpool status -v $TESTPOOL | grep -q 'metascrub_errfile'"

log_must zpool scrub -w -m $TESTPOOL
log_must eval "zpool status -v $TESTPOOL | grep -q 'metascrub_errfile'"

# A clean full scrub rotates the error log and drops the stale entry.
log_must zpool scrub -w $TESTPOOL
log_mustnot eval "zpool status -v $TESTPOOL | grep -q 'metascrub_errfile'"
log_must zpool clear $TESTPOOL

log_pass "Verified that 'zpool scrub -m' only scrubs metadata blocks."
