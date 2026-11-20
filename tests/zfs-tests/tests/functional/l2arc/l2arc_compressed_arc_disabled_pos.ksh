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
. $STF_SUITE/tests/functional/l2arc/l2arc.cfg

#
# DESCRIPTION:
#	With compressed ARC disabled, compressed blocks are read back
#	correctly from the L2ARC, and fall back to the main pool when the
#	L2ARC copy is damaged.
#
#	The cache device stores a compressed block as PSIZE bytes padded to
#	its allocation size, while the (uncompressed) ARC buffer is LSIZE
#	bytes.  If PSIZE is not a multiple of the cache device's allocation
#	size, the L2ARC read goes through a temporary buffer, out of which
#	l2arc_read_done() used to copy LSIZE bytes.  If the L2ARC copy fails
#	its checksum, the read from the main pool used to ask for PSIZE bytes
#	of decompressed data and failed with EIO.
#
# STRATEGY:
#	1. Disable compressed ARC and set up L2ARC rebuild tunables.
#	2. For a cache device ashift of 12 and of 9:
#	3. Create an ashift=9 pool with compression=lz4, recordsize=16k and
#	   embedded_data disabled, and add the cache device.
#	4. Write a highly compressible file (16K LSIZE, 512 byte PSIZE)
#	   and wait for it to be written to the L2ARC.
#	5. Export and import the pool to drop the file from the ARC; the
#	   L2ARC contents are restored by the L2ARC rebuild.
#	6. Read the file back and verify its contents, that the reads were
#	   served by the L2ARC (l2_hits rose) and that there were no L2ARC
#	   checksum errors (l2_cksum_bad).
#	7. Export and import the pool again, inject corruption into reads
#	   from the cache device, and verify the file can still be read
#	   correctly (from the main pool) and that no errors were recorded.
#

verify_runnable "global"

log_assert "L2ARC with compressed ARC disabled reads back compressed blocks."

function cleanup
{
	zinject -c all >/dev/null 2>&1

	if poolexists $TESTPOOL ; then
		destroy_pool $TESTPOOL
	fi

	log_must set_tunable64 COMPRESSED_ARC_ENABLED $carc_enabled
	log_must set_tunable32 L2ARC_NOPREFETCH $noprefetch
	log_must set_tunable32 L2ARC_REBUILD_BLOCKS_MIN_L2SIZE \
		$rebuild_blocks_min_l2size
}
log_onexit cleanup

typeset carc_enabled=$(get_tunable COMPRESSED_ARC_ENABLED)
typeset noprefetch=$(get_tunable L2ARC_NOPREFETCH)
typeset rebuild_blocks_min_l2size=$(get_tunable L2ARC_REBUILD_BLOCKS_MIN_L2SIZE)

log_must set_tunable64 COMPRESSED_ARC_ENABLED 0
log_must set_tunable32 L2ARC_NOPREFETCH 0
log_must set_tunable32 L2ARC_REBUILD_BLOCKS_MIN_L2SIZE 0

typeset file=/$TESTPOOL/file
typeset file_mb=64

# Highly compressible, but not all zeros (which would become holes).
function gen_data
{
	dd if=/dev/zero bs=1024k count=$file_mb 2>/dev/null | tr '\0' 'Z'
}

# Export and import the pool, waiting for the L2ARC rebuild to finish.
function reimport
{
	log_must zpool export $TESTPOOL
	arcstat_quiescence_noecho l2_feeds

	typeset start=$(kstat arcstats.l2_rebuild_bufs)
	log_must zpool import -d $VDIR $TESTPOOL
	arcstat_quiescence_noecho l2_rebuild_bufs
	typeset end=$(kstat arcstats.l2_rebuild_bufs)
	log_note "L2ARC buffers rebuilt: $(( end - start ))"
	log_must test $end -gt $start
}

for cache_ashift in 12 9; do
	log_note "Testing a cache device with ashift=$cache_ashift"

	log_must rm -f $VDEV_CACHE
	log_must truncate -s 256M $VDEV_CACHE

	# Disable embedded_data so that the tiny compressed blocks are not
	# embedded in their block pointers.
	log_must zpool create -f -o ashift=9 -o feature@embedded_data=disabled \
		-O compression=lz4 -O recordsize=16k $TESTPOOL $VDEV
	log_must zpool add -f -o ashift=$cache_ashift $TESTPOOL \
		cache $VDEV_CACHE
	log_must eval "zdb -l $VDEV_CACHE | grep -q 'ashift: $cache_ashift'"

	log_must eval "gen_data > $file"
	sync_pool $TESTPOOL

	# The file must be stored compressed in sub-4K physical blocks.
	typeset objnum=$(get_objnum $file)
	log_must test -n "$objnum"
	typeset bsize=$(zdb -ddddd $TESTPOOL/ $objnum | \
		awk '$2 == "L0" { print $4; exit }')
	log_note "L0 block size: $bsize"
	log_must test "${bsize%/*}" = "4000L"
	typeset psize=${bsize#*/}
	log_must test $(( 16#${psize%P} )) -lt 4096

	arcstat_quiescence_noecho l2_size
	log_must test $(kstat arcstats.l2_size) -gt 0

	# Read the file back from the L2ARC.
	reimport

	typeset l2_hits_start=$(kstat arcstats.l2_hits)
	typeset l2_cksum_bad_start=$(kstat arcstats.l2_cksum_bad)

	log_must eval "gen_data | cmp - $file"

	typeset l2_hits_end=$(kstat arcstats.l2_hits)
	typeset l2_cksum_bad_end=$(kstat arcstats.l2_cksum_bad)

	log_note "L2ARC hits: $(( l2_hits_end - l2_hits_start ))," \
		"checksum errors: $(( l2_cksum_bad_end - l2_cksum_bad_start ))"
	log_must test $l2_hits_end -gt $l2_hits_start
	log_must test $l2_cksum_bad_end -eq $l2_cksum_bad_start

	# Read the file back with the L2ARC copies corrupted.
	reimport

	l2_cksum_bad_start=$(kstat arcstats.l2_cksum_bad)

	log_must zinject -d $VDEV_CACHE -e corrupt -T read -f 100 $TESTPOOL
	log_must eval "gen_data | cmp - $file"
	log_must zinject -c all

	l2_cksum_bad_end=$(kstat arcstats.l2_cksum_bad)

	log_note "L2ARC checksum errors with corruption:" \
		"$(( l2_cksum_bad_end - l2_cksum_bad_start ))"
	log_must test $l2_cksum_bad_end -gt $l2_cksum_bad_start
	log_must eval "zpool status -v $TESTPOOL | grep -q 'No known data errors'"

	log_must zpool destroy -f $TESTPOOL
done

log_pass "L2ARC with compressed ARC disabled reads back compressed blocks."
