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
# Copyright (c) 2024 Klara
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify the status.json kstat
#
# STRATEGY:
# 1. Create zpool
# 2. Confirm the output of the kstat is valid json
# 3. Confirm that some expected keys are present
#

function cleanup
{
	poolexists $TESTPOOL2 && destroy_pool $TESTPOOL2
	log_must rm -f $all_vdevs
	[[ -f $tmpfile ]] && rm -f $tmpfile
}

log_assert "Verify status.json kstat"

log_onexit cleanup

all_vdevs=$(echo $TESTDIR/vdev{1..6})
log_must mkdir -p $TESTDIR
log_must truncate -s $MINVDEVSIZE $all_vdevs
tmpfile=$TEST_BASE_DIR/tmpfile.$$

for raid_type in "draid2:3d:6c:1s" "raidz2"; do

	log_must zpool create -f $TESTPOOL2 $raid_type $all_vdevs

	# Verify that the JSON output is valid
	log_must eval "kstat ${TESTPOOL2}/status.json | python3 -m json.tool > $tmpfile"

	# Verify that some of the expected keys are present
	log_must eval "grep '\"vdev_children\": 6' $tmpfile"
	log_must eval "grep '\"nparity\": 2' $tmpfile"
	log_must eval "grep '\"state\": \"ONLINE\"' $tmpfile"
	log_must eval "grep '\"name\": \"$TESTPOOL2\"' $tmpfile"

	zpool destroy $TESTPOOL2
done

log_pass "Verify status.json kstat"
