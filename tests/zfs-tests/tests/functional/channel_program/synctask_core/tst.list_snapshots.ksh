#!/bin/ksh -p
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

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/channel_program/channel_common.kshlib

#
# DESCRIPTION:
#       Listing zfs snapshots should work correctly.
#

verify_runnable "global"

log_assert "Listing zfs snapshots should work correctly."

function cleanup
{
	destroy_dataset $TESTPOOL/$TESTFS@$TESTSNAP
	destroy_dataset $TESTPOOL/$TESTFS@$TESTSNAP1
	destroy_dataset $TESTPOOL/$TESTFS@$TESTSNAP2
	destroy_dataset $TESTPOOL/$TESTFS@$TESTSNAP3
}

log_onexit cleanup

# 0 snapshots handled correctly
log_must_program $TESTPOOL - <<-EOF
	n = 0
	for s in zfs.list.snapshots("$TESTPOOL/$TESTFS") do
		zfs.debug("ERROR: found snapshot " .. s)
		n = n + 1
	end
	assert(n == 0)
	return 0
EOF

# Take a snapshot, make sure it appears
create_snapshot $TESTPOOL/$TESTFS $TESTSNAP

log_must_program $TESTPOOL - <<-EOF
	n = 0
	for s in zfs.list.snapshots("$TESTPOOL/$TESTFS") do
		assert(s == "$TESTPOOL/$TESTFS@$TESTSNAP")
		n = n + 1
	end
	assert(n == 1)
	return 0
EOF

TESTSNAP1=${TESTSNAP}-1
TESTSNAP2=${TESTSNAP}-2
TESTSNAP3=${TESTSNAP}-3
create_snapshot $TESTPOOL/$TESTFS $TESTSNAP1
create_snapshot $TESTPOOL/$TESTFS $TESTSNAP2
create_snapshot $TESTPOOL/$TESTFS $TESTSNAP3

# All snapshots appear exactly once
log_must_program $TESTPOOL - <<-EOF
	a = {}
	a["$TESTPOOL/$TESTFS@$TESTSNAP"] = false
	a["$TESTPOOL/$TESTFS@$TESTSNAP1"] = false
	a["$TESTPOOL/$TESTFS@$TESTSNAP2"] = false
	a["$TESTPOOL/$TESTFS@$TESTSNAP3"] = false
	n = 0
	for s in zfs.list.snapshots("$TESTPOOL/$TESTFS") do
		assert(not a[s])
		a[s] = true
		n = n + 1
	end
	assert(n == 4)
	assert(a["$TESTPOOL/$TESTFS@$TESTSNAP"] and
	    a["$TESTPOOL/$TESTFS@$TESTSNAP1"] and
	    a["$TESTPOOL/$TESTFS@$TESTSNAP2"] and
	    a["$TESTPOOL/$TESTFS@$TESTSNAP3"])
	return 0
EOF

# Bad input
log_mustnot_program $TESTPOOL - <<-EOF
	zfs.list.snapshots("$TESTPOOL/not-a-fs")
	return 0
EOF

log_mustnot_program $TESTPOOL - <<-EOF
	zfs.list.snapshots("not-a-pool/$TESTFS")
	return 0
EOF

# Can't look in a different pool than the one specified on command line
log_mustnot_program $TESTPOOL - <<-EOF
	zfs.list.snapshots("rpool")
	return 0
EOF

log_mustnot_program $TESTPOOL - <<-EOF
	zfs.list.snapshots("$TESTPOOL/${TESTFS}@$TESTSNAP")
	return 0
EOF

log_pass "Listing zfs snapshots should work correctly."
