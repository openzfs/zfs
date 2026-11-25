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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	A pool whose dedup table holds a copy allocated after a RAID-Z
#	expansion under its pre-expansion birth can be scrubbed without
#	that copy's repair writing into the allocation that follows it.
#
# STRATEGY:
#	1. Import the stored pool and check that target-2's dedup block
#	   pointer still has the mixed-width copy.
#	2. Scrub, re-import, and scrub again, so that a damaged neighbour
#	   is read after the first scrub's repairs.
#	3. Require no checksum errors on any device, and every file to
#	   read back as written.
#
# The pool was created by OpenZFS before this fix (cf1bc78d0) from three
# 64 MiB files with "zpool create -o compatibility=openzfs-2.3 -o ashift=12
# -O dedup=sha256 -O compression=off -O recordsize=128k dedup_overrun raidz1
# ...". target-1 is one 128K block of 'T'. The pool was expanded to four
# devices, then target-2 was written as a copy of target-1 with copies=2,
# extending the dedup entry with a second copy at the new width (0x2c000
# allocated, 0x30000 at the old width). Right after, victim/file was
# written with copies=2: sixteen 128K blocks of 'V', each starting with
# "victim NN". Block 12's first copy starts where target-2's second copy
# ends, so an old-width repair of that copy writes into it.
#

verify_runnable "global"

typeset -r pool=dedup_overrun

function cleanup
{
	poolexists $pool && destroy_pool $pool
	rm -rf $workdir
}

# The contents written when the pool was created.
function expected_victim
{
	typeset -i b
	for ((b = 0; b < 16; b++)); do
		printf 'victim %02d' $b
		head -c 131063 /dev/zero | tr '\0' V
	done
}

log_assert "Scrubbing a mixed-width dedup copy does not overrun its allocation"
workdir=$(mktemp -d $TEST_BASE_DIR/raidz_dedup_overrun.XXXXXX) ||
    log_fail "cannot create test directory"
log_onexit cleanup

typeset -r blockfiles=$STF_SUITE/tests/functional/raidz/blockfiles
for i in 0 1 2 3; do
	log_must eval "bzcat $blockfiles/raidz_dedup_overrun-$i.dat.bz2" \
	    "> $workdir/dedup_overrun-$i.dat"
done
head -c 131072 /dev/zero | tr '\0' T > $workdir/target
expected_victim > $workdir/victim

log_must zpool import -d $workdir $pool
log_must eval "zdb -ddddddbbbbbb $pool/ $(get_objnum /$pool/target-2) |
    grep -q 'L0 DVA\[0\]=<0:[0-9a-f]*:30000> DVA\[1\]=<0:[0-9a-f]*:2c000>'"

log_must zpool scrub -w $pool
log_must zpool export $pool
log_must zpool import -d $workdir $pool
log_must zpool scrub -w $pool

# The rejected copy is a read error of the RAID-Z vdev; a repair written
# past it shows up as checksum errors on the devices.
log_must eval "zpool status -p $pool > $workdir/status"
awk -v dir="$workdir/" 'index($1, dir) == 1 { n++; if ($5 != 0) bad = 1 }
    END { exit (bad || n != 4) }' $workdir/status ||
    log_fail "devices missing or with checksum errors: $(cat $workdir/status)"
log_must check_pool_status $pool "errors" "No known data errors"
log_must cmp $workdir/target /$pool/target-1
log_must cmp $workdir/target /$pool/target-2
log_must cmp $workdir/victim /$pool/victim/file

log_pass "Scrubbing a mixed-width dedup copy did not overrun its allocation"
