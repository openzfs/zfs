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
#       Listing zfs clones should work correctly.
#

verify_runnable "global"

log_assert "Listing zfs clones should work correctly."

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS@$TESTSNAP && \
	    log_must zfs destroy -R $TESTPOOL/$TESTFS@$TESTSNAP
}

log_onexit cleanup

# Take snapshot to test with ($TESTSNAP)
create_snapshot

# 0 clones handled correctly
log_must_program $TESTPOOL - <<-EOF
	n = 0
	for s in zfs.list.clones("$TESTPOOL/$TESTFS@$TESTSNAP") do
		n = n + 1
	end
	assert(n == 0)
	return 0
EOF

# Create a clone ($TESTCLONE)
create_clone

log_must_program $TESTPOOL - <<-EOF
	n = 0
	for s in zfs.list.clones("$TESTPOOL/$TESTFS@$TESTSNAP") do
		assert(s == "$TESTPOOL/$TESTCLONE")
		n = n + 1
	end
	assert(n == 1)
	return 0
EOF

TESTCLONE1=${TESTCLONE}-1
TESTCLONE2=${TESTCLONE}-2
TESTCLONE3=${TESTCLONE}-3
create_clone $TESTPOOL/$TESTFS@$TESTSNAP $TESTPOOL/$TESTCLONE1
create_clone $TESTPOOL/$TESTFS@$TESTSNAP $TESTPOOL/$TESTCLONE2
create_clone $TESTPOOL/$TESTFS@$TESTSNAP $TESTPOOL/$TESTCLONE3

# All clones appear exactly once
log_must_program $TESTPOOL - <<-EOF
	a = {}
	a["$TESTPOOL/$TESTCLONE"] = false
	a["$TESTPOOL/$TESTCLONE1"] = false
	a["$TESTPOOL/$TESTCLONE2"] = false
	a["$TESTPOOL/$TESTCLONE3"] = false
	n = 0
	for s in zfs.list.clones("$TESTPOOL/$TESTFS@$TESTSNAP") do
		assert(not a[s])
		a[s] = true
		n = n + 1
	end
	assert(n == 4)
	assert(a["$TESTPOOL/$TESTCLONE"] and
	    a["$TESTPOOL/$TESTCLONE1"] and
	    a["$TESTPOOL/$TESTCLONE2"] and
	    a["$TESTPOOL/$TESTCLONE3"])
	return 0
EOF

# Bad input
log_mustnot_program $TESTPOOL - <<-EOF
	zfs.list.clones("$TESTPOOL/not-a-fs@$TESTSNAP")
	return 0
EOF

log_mustnot_program $TESTPOOL - <<-EOF
	zfs.list.clones("$TESTPOOL/$TESTFS@not-a-snap")
	return 0
EOF

# Can't look in a different pool than the one specified on command line
log_mustnot_program $TESTPOOL - <<-EOF
	zfs.list.clones("rpool")
	return 0
EOF

log_mustnot_program $TESTPOOL - <<-EOF
	zfs.list.clones("not-a-pool/$TESTFS@$TESTSNAP")
	return 0
EOF

log_mustnot_program $TESTPOOL - <<-EOF
	zfs.list.clones("$TESTPOOL/$TESTFS")
	return 0
EOF

log_pass "Listing zfs clones should work correctly."
