#!/bin/ksh -p
#
# CDDL HEADER START
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
# CDDL HEADER END
#

#
# Copyright (c) 2017 by Lawrence Livermore National Security, LLC.
#

# DESCRIPTION:
#	Ensure that MMP updates uberblocks with MMP info at expected intervals. 
#
# STRATEGY:
#	1. Set TXG_TIMEOUT to large value
#	2. Create a zpool
#	3. Clear multihost history
#	4. Sleep, then collect count of uberblocks written
#	5. If number of changes seen is less than min threshold, then fail
#	6. If number of changes seen is more than max threshold, then fail
#	7. Sequence number increments when no TXGs are syncing
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/mmp/mmp.cfg
. $STF_SUITE/tests/functional/mmp/mmp.kshlib

verify_runnable "both"

UBER_CHANGES=0
EXPECTED=$(($(echo $DISKS | wc -w) * 10))
FUDGE=$((EXPECTED * 20 / 100))
MIN_UB_WRITES=$((EXPECTED - FUDGE))
MAX_UB_WRITES=$((EXPECTED + FUDGE))
MIN_SEQ_VALUES=7

function cleanup
{
	default_cleanup_noexit
	log_must set_tunable64 MULTIHOST_INTERVAL $MMP_INTERVAL_DEFAULT
	set_tunable64 TXG_TIMEOUT $TXG_TIMEOUT_DEFAULT
	log_must mmp_clear_hostid
}

log_assert "Ensure MMP uberblocks update at the correct interval"
log_onexit cleanup

log_must set_tunable64 TXG_TIMEOUT $TXG_TIMEOUT_LONG
log_must mmp_set_hostid $HOSTID1

default_setup_noexit "$DISKS"
log_must zpool set multihost=on $TESTPOOL
clear_mmp_history
UBER_CHANGES=$(count_mmp_writes $TESTPOOL 10)

log_note "Uberblock changed $UBER_CHANGES times"

if [ $UBER_CHANGES -lt $MIN_UB_WRITES ]; then
	log_fail "Fewer uberblock writes occurred than expected ($EXPECTED)"
fi

if [ $UBER_CHANGES -gt $MAX_UB_WRITES ]; then
	log_fail "More uberblock writes occurred than expected ($EXPECTED)"
fi

log_must set_tunable64 MULTIHOST_INTERVAL $MMP_INTERVAL_MIN
SEQ_BEFORE=$(zdb -luuuu ${DISK[0]} | awk '/mmp_seq/ {if ($NF>max) max=$NF}; END {print max}')
sleep 1
SEQ_AFTER=$(zdb  -luuuu ${DISK[0]} | awk '/mmp_seq/ {if ($NF>max) max=$NF}; END {print max}')
if [ $((SEQ_AFTER - SEQ_BEFORE)) -lt $MIN_SEQ_VALUES ]; then
	zdb -luuuu ${DISK[0]}
	log_fail "ERROR: mmp_seq did not increase by $MIN_SEQ_VALUES; before $SEQ_BEFORE after $SEQ_AFTER"
fi

log_pass "Ensure MMP uberblocks update at the correct interval passed"
