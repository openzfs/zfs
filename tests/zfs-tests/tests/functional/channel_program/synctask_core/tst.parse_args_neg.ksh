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
#	Try calling zfs.sync.destroy with various arguments that are not
#	valid.  The script should fail.
#

verify_runnable "global"

set -A progs "zfs.sync.destroy(\"foo\", \"bar\")" \
	"zfs.sync.destroy(\"foo\", 12345)" \
	"zfs.sync.destroy(12345)" \
	"zfs.sync.destroy()" \
	"zfs.sync.destroy{\"foo\", bar=true}" \
	"zfs.sync.destroy{\"foo\", defer=12345}" \
	"zfs.sync.destroy{\"foo\", defer=true, 12345}" \
	"zfs.sync.destroy{\"foo\", defer=true, bar=12345}" \
	"zfs.sync.destroy{\"foo\", bar=true, defer=true}" \
	"zfs.sync.destroy{defer=true}" \
	"zfs.sync.destroy{12345, defer=true}"


typeset -i i=0
while (( i < ${#progs[*]} )); do
	log_note "running program: ${progs[i]}"
	# output should contain the usage message, which starts with "destroy{"
	echo ${progs[i]} | log_mustnot_checkerror_program "destroy{" $TESTPOOL -
	((i = i + 1))
done

log_pass "Invalid arguments to zfs.sync.destroy generate errors."
