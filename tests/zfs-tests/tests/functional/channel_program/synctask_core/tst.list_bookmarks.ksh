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
#       Listing zfs bookmarks should work correctly.
#

verify_runnable "global"

TESTBOOK=$TESTPOOL/$TESTFS#testbook
TESTBOOK1=$TESTBOOK-1
TESTBOOK2=$TESTBOOK-2
TESTBOOK3=$TESTBOOK-3

function cleanup
{
	bkmarkexists $TESTBOOK && log_must zfs destroy $TESTBOOK
	bkmarkexists $TESTBOOK1 && log_must zfs destroy $TESTBOOK1
	bkmarkexists $TESTBOOK2 && log_must zfs destroy $TESTBOOK2
	bkmarkexists $TESTBOOK3 && log_must zfs destroy $TESTBOOK3
	destroy_snapshot
}

log_onexit cleanup

create_snapshot

# 0 bookmarks handled correctly
log_must_program $TESTPOOL - <<-EOF
	n = 0
	for s in zfs.list.bookmarks("$TESTPOOL/$TESTFS") do
		n = n + 1
	end
	assert(n == 0)
	return 0
EOF

# Create a bookmark
log_must zfs bookmark $TESTPOOL/$TESTFS@$TESTSNAP $TESTBOOK

log_must_program $TESTPOOL - <<-EOF
	n = 0
	for s in zfs.list.bookmarks("$TESTPOOL/$TESTFS") do
		assert(s == "$TESTBOOK")
		n = n + 1
	end
	assert(n == 1)
	return 0
EOF

log_must zfs bookmark $TESTPOOL/$TESTFS@$TESTSNAP $TESTBOOK1
log_must zfs bookmark $TESTPOOL/$TESTFS@$TESTSNAP $TESTBOOK2
log_must zfs bookmark $TESTPOOL/$TESTFS@$TESTSNAP $TESTBOOK3

# All bookmarks appear exactly once
log_must_program $TESTPOOL - <<-EOF
	a = {}
	a["$TESTBOOK"] = false
	a["$TESTBOOK1"] = false
	a["$TESTBOOK2"] = false
	a["$TESTBOOK3"] = false
	n = 0
	for s in zfs.list.bookmarks("$TESTPOOL/$TESTFS") do
		assert(not a[s])
		a[s] = true
		n = n + 1
	end
	assert(n == 4)
	assert(a["$TESTBOOK"] and
	    a["$TESTBOOK1"] and
	    a["$TESTBOOK2"] and
	    a["$TESTBOOK3"])
	return 0
EOF

# Nonexistent input
log_mustnot_program $TESTPOOL - <<-EOF
	zfs.list.bookmarks("$TESTPOOL/nonexistent-fs")
	return 0
EOF
log_mustnot_program $TESTPOOL - <<-EOF
	zfs.list.bookmarks("nonexistent-pool/$TESTFS")
	return 0
EOF

# Can't look in a different pool than the one specified on command line
log_mustnot_program $TESTPOOL - <<-EOF
	zfs.list.bookmarks("testpool2")
	return 0
EOF

# Can't have bookmarks on snapshots, only on filesystems
log_mustnot_program $TESTPOOL - <<-EOF
	zfs.list.bookmarks("$TESTPOOL/$TESTFS@$TESTSNAP")
	return 0
EOF

# Can't have bookmarks on bookmarks, only on filesystems
log_mustnot_program $TESTPOOL - <<-EOF
	zfs.list.bookmarks("$TESTBOOK")
	return 0
EOF

log_pass "Listing zfs bookmarks should work correctly."
