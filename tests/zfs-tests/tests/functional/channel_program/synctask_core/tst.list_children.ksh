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
#       Listing zfs children should work correctly.
#

verify_runnable "global"

log_assert "Listing zfs children should work correctly."

TESTCHILD=$TESTPOOL/$TESTFS/testchild
TESTCHILD1=$TESTCHILD-1
TESTCHILD2=$TESTCHILD-2
TESTCHILD3=$TESTCHILD-3

function cleanup
{
	destroy_dataset $TESTCHILD
	destroy_dataset $TESTCHILD1
	destroy_dataset $TESTCHILD2
	destroy_dataset $TESTCHILD3
}

log_onexit cleanup

# 0 children handled correctly
log_must_program $TESTPOOL - <<-EOF
	n = 0
	for s in zfs.list.children("$TESTPOOL/$TESTFS") do
		n = n + 1
	end
	assert(n == 0)
	return 0
EOF

log_must_program $TESTPOOL - <<-EOF
	n = 0
	for s in zfs.list.children("$TESTPOOL") do
		assert(s == "$TESTPOOL/$TESTFS")
		n = n + 1
	end
	assert(n == 1)
	return 0
EOF

# Create a child fs
log_must zfs create $TESTCHILD

log_must_program $TESTPOOL - <<-EOF
	n = 0
	for s in zfs.list.children("$TESTPOOL/$TESTFS") do
		assert(s == "$TESTCHILD")
		n = n + 1
	end
	assert(n == 1)
	return 0
EOF

log_must zfs create $TESTCHILD1
log_must zfs create $TESTCHILD2
log_must zfs create $TESTCHILD3

# All children appear exactly once
log_must_program $TESTPOOL - <<-EOF
	a = {}
	a["$TESTCHILD"] = false
	a["$TESTCHILD1"] = false
	a["$TESTCHILD2"] = false
	a["$TESTCHILD3"] = false
	n = 0
	for s in zfs.list.children("$TESTPOOL/$TESTFS") do
		assert(not a[s])
		a[s] = true
		n = n + 1
	end
	assert(n == 4)
	assert(a["$TESTCHILD"] and
	    a["$TESTCHILD1"] and
	    a["$TESTCHILD2"] and
	    a["$TESTCHILD3"])
	return 0
EOF

# Bad input
log_mustnot_program $TESTPOOL - <<-EOF
	zfs.list.children("$TESTPOOL/not-a-fs")
	return 0
EOF

log_mustnot_program $TESTPOOL - <<-EOF
	zfs.list.children("not-a-pool/$TESTFS")
	return 0
EOF

# Can't look in a different pool than the one specified on command line
log_mustnot_program $TESTPOOL - <<-EOF
	zfs.list.children("rpool")
	return 0
EOF

create_snapshot
log_mustnot_program $TESTPOOL - <<-EOF
	zfs.list.children("$TESTPOOL/$TESTFS@$TESTSNAP")
	return 0
EOF
destroy_snapshot

log_pass "Listing zfs children should work correctly."
