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
# zpool_influxdb reports the remaining time of a running scan in seconds:
# the bytes still to issue divided by the issue rate, as 'zpool status'
# estimates it.
#
# STRATEGY:
# 1. Write some data and slow down the pool's disks with zinject.
# 2. Start a scrub.
# 3. While it issues I/O, sample zpool_influxdb's scan line and check that
#    remaining_t is close to (to_examine - skipped - issued) * elapsed /
#    pass_issued, computed from the same line (elapsed from its timestamp).
#

typeset tmpfile=$TEST_BASE_DIR/zpool_influxdb_scan.out.$$

function cleanup
{
	zinject -c all >/dev/null
	zpool scrub -s $TESTPOOL 2>/dev/null
	rm -f $tmpfile $TESTDIR/file
}
log_onexit cleanup

function field # <name>
{
	grep -o ",$1=[0-9]*" $tmpfile | cut -d= -f2
}

log_assert "zpool_influxdb reports the remaining scan time in seconds"

log_must dd if=/dev/urandom of=$TESTDIR/file bs=1M count=256
sync_pool $TESTPOOL
for disk in $DISKS; do
	log_must zinject -d $disk -D 100:1 $TESTPOOL
done
log_must zpool scrub $TESTPOOL

typeset -i checked=0 i
for i in {1..120}; do
	sleep 1
	zpool_influxdb --no-histograms $TESTPOOL |
	    grep '^zpool_scan_stats,.*state=scanning ' > $tmpfile || break
	log_note "$(cat $tmpfile)"
	typeset -i to_examine=$(field to_examine) skipped=$(field skipped)
	typeset -i issued=$(field issued) pass_issued=$(field pass_issued)
	typeset -i start=$(field start_ts) paused=$(field paused_t)
	typeset -i remaining=$(field remaining_t)
	typeset -i now=$(( $(awk '{print $NF}' $tmpfile) / 1000000000 ))
	typeset -i elapsed=$(( now - start - paused ))
	typeset -i left=$(( to_examine - skipped - issued ))
	(( elapsed > 0 && pass_issued > 0 && left > 0 )) || continue
	typeset -i want=$(( left * elapsed / pass_issued ))
	(( want >= 4 )) || continue
	if (( remaining < want - want / 4 - 2 ||
	    remaining > want + want / 4 + 2 )); then
		log_fail "remaining_t=$remaining, expected about $want" \
		    "($left bytes left at $pass_issued bytes in $elapsed s)"
	fi
	(( checked += 1 ))
	(( checked >= 3 )) && break
done
(( checked > 0 )) ||
    log_unsupported "the scrub finished before it could be sampled"

log_pass "zpool_influxdb reports the remaining scan time in seconds"
