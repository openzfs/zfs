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
# Copyright (c) 2017 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/channel_program/channel_common.kshlib

#
# DESCRIPTION:
#       Listing zfs holds should work correctly.
#

verify_runnable "global"

TESTHOLD=testhold-tag
TESTHOLD1=$TESTHOLD-1
TESTHOLD2=$TESTHOLD-2
TESTHOLD3=$TESTHOLD-3
SNAP=$TESTPOOL/$TESTFS@$TESTSNAP

function cleanup
{
	holdexists $TESTHOLD $SNAP && log_must zfs release $TESTHOLD $SNAP
	holdexists $TESTHOLD1 $SNAP && log_must zfs release $TESTHOLD1 $SNAP
	holdexists $TESTHOLD2 $SNAP && log_must zfs release $TESTHOLD2 $SNAP
	holdexists $TESTHOLD3 $SNAP && log_must zfs release $TESTHOLD3 $SNAP
	destroy_snapshot
}

log_onexit cleanup

create_snapshot

# 0 holds handled correctly
log_must_program $TESTPOOL - <<-EOF
	n = 0
	for s in zfs.list.holds("$SNAP") do
		n = n + 1
	end
	assert(n == 0)
	return 0
EOF

# Create a hold
log_must zfs hold $TESTHOLD $SNAP

log_must_program $TESTPOOL - <<-EOF
	n = 0
	for s in zfs.list.holds("$SNAP") do
		assert(s == "$TESTHOLD")
		n = n + 1
	end
	assert(n == 1)
	return 0
EOF

log_must zfs hold $TESTHOLD1 $SNAP
log_must zfs hold $TESTHOLD2 $SNAP
log_must zfs hold $TESTHOLD3 $SNAP

# All holds appear exactly once
log_must_program $TESTPOOL - <<-EOF
	a = {}
	a["$TESTHOLD"] = false
	a["$TESTHOLD1"] = false
	a["$TESTHOLD2"] = false
	a["$TESTHOLD3"] = false
	n = 0
	for s in zfs.list.holds("$SNAP") do
		assert(not a[s])
		a[s] = true
		n = n + 1
	end
	assert(n == 4)
	assert(a["$TESTHOLD"] and
	    a["$TESTHOLD1"] and
	    a["$TESTHOLD2"] and
	    a["$TESTHOLD3"])
	return 0
EOF

# Nonexistent input
log_mustnot_program $TESTPOOL - <<-EOF
	zfs.list.holds("$TESTPOOL/nonexistent-fs@nonexistent-snap")
	return 0
EOF
log_mustnot_program $TESTPOOL - <<-EOF
	zfs.list.holds("nonexistent-pool/$TESTFS")
	return 0
EOF

# Can't look in a different pool than the one specified on command line
log_mustnot_program $TESTPOOL - <<-EOF
	zfs.list.holds("testpool2")
	return 0
EOF

# Can't have holds on filesystems
log_mustnot_program $TESTPOOL - <<-EOF
	zfs.list.holds("$TESTPOOL/$TESTFS")
	return 0
EOF

# Can't have holds on bookmarks
log_mustnot_program $TESTPOOL - <<-EOF
	zfs.list.holds("$TESTPOOL/$TESTFS#bookmark")
	return 0
EOF

log_pass "Listing zfs holds should work correctly."
