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
# Copyright 2020 Joyent, Inc.
#

. $STF_SUITE/tests/functional/channel_program/channel_common.kshlib

verify_runnable "global"

fs=$TESTPOOL/$TESTFS
testprop="com.joyent:testprop"
testval="testval"

log_must dataset_setprop $fs $testprop $testval
log_must_program_sync $TESTPOOL - $fs $testprop <<-EOF
	arg = ...
	fs = arg["argv"][1]
	prop = arg["argv"][2]
	err = zfs.sync.inherit(fs, prop)
	msg = "resetting " .. prop .. " on " .. fs .. " err=" .. err
	return msg
EOF


prop=$(get_prop $testprop $fs)
[[ "$prop" == "-" ]] || log_fail "Property still set after inheriting"

log_pass "Inherit/clear property with channel program works."
