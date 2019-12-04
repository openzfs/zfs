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
#       Listing zfs user properties should work correctly.
#
#       Note, that this file tests both zfs.list.user_properties
#       and it's alias zfs.list.properties.
#

verify_runnable "global"

TESTPROP="org.openzfs:test_property"
TESTPROP1=$TESTPROP-1
TESTPROP2=$TESTPROP-2
TESTPROP3=$TESTPROP-3
TESTPROP4=$TESTPROP-4

TESTVAL="true"
TESTVAL1="busy"
TESTVAL2="9223372036854775808"
TESTVAL3="801f2266-3715-41f4-9080-3d5e913b0f15"
TESTVAL4="TOZwOfACvQtmDyiq68elB3a3g9YYyxBjSnLtN3ZyQYNOAKykzIE2khKKOBncJiDx"


# 0 properties handled correctly
log_must_program $TESTPOOL - <<-EOF
	n = 0
	for p in zfs.list.user_properties("$TESTPOOL/$TESTFS") do
		n = n + 1
	end
	assert(n == 0)
	return 0
EOF
log_must_program $TESTPOOL - <<-EOF
	n = 0
	for p in zfs.list.properties("$TESTPOOL/$TESTFS") do
		n = n + 1
	end
	assert(n == 0)
	return 0
EOF

# Add a single user property
log_must zfs set $TESTPROP="$TESTVAL" $TESTPOOL/$TESTFS

log_must_program $TESTPOOL - <<-EOF
	n = 0
	for p,v in zfs.list.user_properties("$TESTPOOL/$TESTFS") do
		assert(p == "$TESTPROP")
		assert(v == "$TESTVAL")
		n = n + 1
	end
	assert(n == 1)
	return 0
EOF
log_must_program $TESTPOOL - <<-EOF
	n = 0
	for p,v in zfs.list.properties("$TESTPOOL/$TESTFS") do
		assert(p == "$TESTPROP")
		assert(v == "$TESTVAL")
		n = n + 1
	end
	assert(n == 1)
	return 0
EOF

log_must zfs set $TESTPROP1="$TESTVAL1" $TESTPOOL/$TESTFS
log_must zfs set $TESTPROP2="$TESTVAL2" $TESTPOOL/$TESTFS
log_must zfs set $TESTPROP3="$TESTVAL3" $TESTPOOL/$TESTFS
log_must zfs set $TESTPROP4="$TESTVAL4" $TESTPOOL/$TESTFS

# All user properties have correct value and appear exactly once
log_must_program $TESTPOOL - <<-EOF
	a = {}
	a["$TESTPROP"] = false
	a["$TESTPROP1"] = false
	a["$TESTPROP2"] = false
	a["$TESTPROP3"] = false
	a["$TESTPROP4"] = false
	m = {}
	m["$TESTPROP"] = "$TESTVAL"
	m["$TESTPROP1"] = "$TESTVAL1"
	m["$TESTPROP2"] = "$TESTVAL2"
	m["$TESTPROP3"] = "$TESTVAL3"
	m["$TESTPROP4"] = "$TESTVAL4"
	n = 0
	for p,v in zfs.list.user_properties("$TESTPOOL/$TESTFS") do
		assert(not a[p])
		a[p] = true
		assert(v == m[p])
		n = n + 1
	end
	assert(n == 5)
	assert(a["$TESTPROP"] and
	    a["$TESTPROP1"] and
	    a["$TESTPROP2"] and
	    a["$TESTPROP3"] and
	    a["$TESTPROP4"])
	return 0
EOF
log_must_program $TESTPOOL - <<-EOF
	a = {}
	a["$TESTPROP"] = false
	a["$TESTPROP1"] = false
	a["$TESTPROP2"] = false
	a["$TESTPROP3"] = false
	a["$TESTPROP4"] = false
	m = {}
	m["$TESTPROP"] = "$TESTVAL"
	m["$TESTPROP1"] = "$TESTVAL1"
	m["$TESTPROP2"] = "$TESTVAL2"
	m["$TESTPROP3"] = "$TESTVAL3"
	m["$TESTPROP4"] = "$TESTVAL4"
	n = 0
	for p,v in zfs.list.properties("$TESTPOOL/$TESTFS") do
		assert(not a[p])
		a[p] = true
		assert(v == m[p])
		n = n + 1
	end
	assert(n == 5)
	assert(a["$TESTPROP"] and
	    a["$TESTPROP1"] and
	    a["$TESTPROP2"] and
	    a["$TESTPROP3"] and
	    a["$TESTPROP4"])
	return 0
EOF

log_pass "Listing zfs user properties should work correctly."
