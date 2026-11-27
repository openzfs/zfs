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
#	A cache device whose writes fail, and whose probe then fails, is
#	faulted, as a data device is.
#
# STRATEGY:
#	1. Create a pool with a cache device, create random files in it
#		and random read them, and verify that L2ARC writes happen.
#	2. Inject I/O errors into the writes and the probes of the cache
#		device.
#	3. Rewrite the files and random read them, so that the L2ARC
#		writes to the cache device again.
#	4. Verify that the cache device is FAULTED within 60 seconds.
#	5. Clear the injection, and verify that zpool clear brings the
#		cache device back ONLINE.
#

verify_runnable "global"

command -v fio > /dev/null || log_unsupported "fio missing"

log_assert "A cache device whose probe failed is faulted."

function cleanup
{
	log_must zinject -c all

	if poolexists $TESTPOOL ; then
		destroy_pool $TESTPOOL
	fi

	log_must set_tunable32 L2ARC_NOPREFETCH $noprefetch
}
log_onexit cleanup

#
# Wait at most 60 seconds for the L2ARC to write more than $1 bytes in all.
# This pool has the only cache device, so the global kstat is its own.
#
function wait_l2arc_writes # bytes
{
	typeset -i tries=0
	while (( $(kstat arcstats.l2_write_bytes) <= $1 && tries < 60 )); do
		sleep 1
		(( tries += 1 ))
	done
	(( $(kstat arcstats.l2_write_bytes) > $1 ))
}

# Wait at most 60 seconds for the cache device to be in state $1.
function wait_cache_state # state
{
	typeset -i tries=0
	while [[ $(get_device_state $TESTPOOL $VDEV_CACHE cache) != $1 ]] &&
	    (( tries < 60 )); do
		sleep 1
		(( tries += 1 ))
	done
	[[ $(get_device_state $TESTPOOL $VDEV_CACHE cache) == $1 ]]
}

# L2ARC_NOPREFETCH is set to 0 to let L2ARC handle prefetches
typeset noprefetch=$(get_tunable L2ARC_NOPREFETCH)
log_must set_tunable32 L2ARC_NOPREFETCH 0

# Small enough that the files can be rewritten in place in the 1 GiB pool.
typeset fill_mb=200
typeset cache_sz=$(( 1.4 * $fill_mb ))
export FILE_SIZE=$(( floor($fill_mb / $NUMJOBS) ))M

log_must truncate -s ${cache_sz}M $VDEV_CACHE

log_must zpool create -f $TESTPOOL $VDEV cache $VDEV_CACHE

typeset written=$(kstat arcstats.l2_write_bytes)
log_must fio $FIO_SCRIPTS/mkfiles.fio
log_must fio $FIO_SCRIPTS/random_reads.fio
wait_l2arc_writes $written || \
    log_fail "Nothing was written to the cache device within 60 seconds."

log_must zinject -d $VDEV_CACHE -e io -T write $TESTPOOL
log_must zinject -d $VDEV_CACHE -e io -T probe $TESTPOOL

# Rewritten blocks are new buffers, not yet in the L2ARC.
log_must fio $FIO_SCRIPTS/mkfiles.fio
log_must fio $FIO_SCRIPTS/random_reads.fio

if ! wait_cache_state FAULTED; then
	zpool status -v $TESTPOOL
	log_fail "The cache device is" \
	    "'$(get_device_state $TESTPOOL $VDEV_CACHE cache)'" \
	    "60 seconds after its probe failed, expected 'FAULTED'."
fi

log_must zinject -c all
log_must zpool clear $TESTPOOL $VDEV_CACHE
wait_cache_state ONLINE || log_fail "The cache device is" \
    "'$(get_device_state $TESTPOOL $VDEV_CACHE cache)' after zpool clear," \
    "expected 'ONLINE'."

log_must zpool destroy -f $TESTPOOL

log_pass "A cache device whose probe failed is faulted."
