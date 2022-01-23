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
#	Ensure that the MMP thread is notified when MULTIHOST_INTERVAL is
#	reduced, and that changes to MULTIHOST_INTERVAL and
#	MULTIHOST_FAIL_INTERVALS do not trigger pool suspensions.
#
# STRATEGY:
#	1. Set MULTIHOST_INTERVAL to much longer than the test duration
#	2. Create a zpool and enable multihost
#	3. Verify no MMP writes occurred
#	4. Set MULTIHOST_INTERVAL to 1 second
#	5. Sleep briefly
#	6. Verify MMP writes began
#	7. Verify mmp_fail and mmp_write in uberblock reflect tunables
#	8. Repeatedly change tunables relating to pool suspension
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/mmp/mmp.cfg
. $STF_SUITE/tests/functional/mmp/mmp.kshlib

verify_runnable "both"

function cleanup
{
	default_cleanup_noexit
	log_must set_tunable64 MULTIHOST_INTERVAL $MMP_INTERVAL_DEFAULT
	log_must set_tunable64 MULTIHOST_FAIL_INTERVALS \
	    $MMP_FAIL_INTERVALS_DEFAULT
	log_must mmp_clear_hostid
}

log_assert "mmp threads notified when MULTIHOST_INTERVAL reduced"
log_onexit cleanup

log_must set_tunable64 MULTIHOST_INTERVAL $MMP_INTERVAL_HOUR
log_must mmp_set_hostid $HOSTID1

default_setup_noexit $DISK
log_must zpool set multihost=on $TESTPOOL

clear_mmp_history
log_must set_tunable64 MULTIHOST_INTERVAL $MMP_INTERVAL_DEFAULT
uber_count=$(count_mmp_writes $TESTPOOL 1)

if [ $uber_count -eq 0 ]; then
	log_fail "ERROR: mmp writes did not start when MULTIHOST_INTERVAL reduced"
fi

# 7. Verify mmp_write and mmp_fail are written
for fails in $(seq $MMP_FAIL_INTERVALS_MIN $((MMP_FAIL_INTERVALS_MIN*2))); do
	for interval in $(seq $MMP_INTERVAL_MIN 200 $MMP_INTERVAL_DEFAULT); do
		log_must set_tunable64 MULTIHOST_FAIL_INTERVALS $fails
		log_must set_tunable64 MULTIHOST_INTERVAL $interval
		sync_pool $TESTPOOL
		typeset mmp_fail=$(zdb $TESTPOOL 2>/dev/null |
		    awk '/mmp_fail/ {print $NF}')
		if [ $fails -ne $mmp_fail ]; then
			log_fail "ERROR: mmp_fail $mmp_fail != $fails"
		fi
		typeset mmp_write=$(zdb $TESTPOOL 2>/dev/null |
		    awk '/mmp_write/ {print $NF}')
		if [ $interval -ne $mmp_write ]; then
			log_fail "ERROR: mmp_write $mmp_write != $interval"
		fi
	done
done


# 8. Repeatedly change MULTIHOST_INTERVAL and fail_intervals
for x in $(seq 10); do
	typeset new_interval=$(( (RANDOM % 20 + 1) * $MMP_INTERVAL_MIN ))
	log_must set_tunable64 MULTIHOST_INTERVAL $new_interval
	typeset action=$((RANDOM %10))
	if [ $action -eq 0 ]; then
		log_must zpool export -a
		log_must mmp_clear_hostid
		log_must mmp_set_hostid $HOSTID1
		log_must zpool import $TESTPOOL
	elif [ $action -eq 1 ]; then
		log_must zpool export -F $TESTPOOL
		log_must zpool import $TESTPOOL
	elif [ $action -eq 2 ]; then
		log_must zpool export -F $TESTPOOL
		log_must mmp_clear_hostid
		log_must mmp_set_hostid $HOSTID2
		log_must zpool import -f $TESTPOOL
	elif [ $action -eq 3 ]; then
		log_must zpool export -F $TESTPOOL
		log_must set_tunable64 MULTIHOST_INTERVAL $MMP_INTERVAL_MIN
		log_must zpool import $TESTPOOL
	elif [ $action -eq 4 ]; then
		log_must set_tunable64 MULTIHOST_FAIL_INTERVALS \
		    $((RANDOM % MMP_FAIL_INTERVALS_DEFAULT))
	fi
	sleep 5
done


log_pass "mmp threads notified when MULTIHOST_INTERVAL reduced"
