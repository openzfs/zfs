#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright (c) 2025, Klara, Inc.
#

#
# This test sets various injections, does some IO to trigger them. and then
# checks the "match" and "inject" counters on the injection records to ensure
# that they're being counted properly.
#
# Note that this is a test of the counters, not injection generally. We're
# usually only looking for the counters moving at all, not caring too much
# about their actual values.

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

log_assert "Check zinject counts are displayed and advanced as expected."

DISK1=${DISKS%% *}

function cleanup
{
	zinject -c all
	default_cleanup_noexit
}

log_onexit cleanup

default_mirror_setup_noexit $DISKS

# Call zinject, get the match and inject counts, and make sure they look
# plausible for the requested frequency.
function check_count_freq
{
	typeset -i freq=$1

	# assuming a single rule, with the match and inject counts in the
	# last two columns
	typeset rule=$(zinject | grep -m 1 -oE '^ *[0-9].*[0-9]$')

	log_note "check_count_freq: using rule: $rule"

	typeset -a record=($(echo $rule | grep -oE ' [0-9]+ +[0-9]+$'))
	typeset -i match=${record[0]}
	typeset -i inject=${record[1]}

	log_note "check_count_freq: freq=$freq match=$match inject=$inject"

	# equality check, for 100% frequency, or if we've never matched the rule
	if [[ $match -eq 0 || $freq -eq 100 ]] ; then
		return [[ $match -eq 0 $inject ]]
	fi

	# Compute the expected injection count, and compare. Because we're
	# not testing the fine details here, it's considered good-enough for
	# the injection account to be within +/- 10% of the expected count.
	typeset -i expect=$(($match * $freq / 100))
	typeset -i diff=$((($expect - $inject) / 10))
	return [[ $diff -ge -1 && $diff -le 1 ]]
}

# Test device IO injections by injecting write errors, doing some writes,
# and making sure the count moved
function test_device_injection
{
	for freq in 100 50 ; do
		log_must zinject -d $DISK1 -e io -T write -f $freq $TESTPOOL

		log_must dd if=/dev/urandom of=/$TESTPOOL/file bs=1M count=1
		log_must zpool sync

		log_must check_count_freq $freq

		log_must zinject -c all
	done
}

# Test object injections by writing a file, injecting checksum errors and
# trying to read it back
function test_object_injection
{
	log_must dd if=/dev/urandom of=/$TESTPOOL/file bs=1M count=1
	zpool sync

	for freq in 100 50 ; do
		log_must zinject -t data -e checksum -f $freq /$TESTPOOL/file

		cat /tank/file > /dev/null || true

		log_must check_count_freq $freq

		log_must zinject -c all
	done
}

# Test delay injections, by injecting delays and writing
function test_delay_injection
{
	for freq in 100 50 ; do
		log_must zinject -d $DISK1 -D 50:1 -f $freq $TESTPOOL

		log_must dd if=/dev/urandom of=/$TESTPOOL/file bs=1M count=1
		zpool sync

		log_must check_count_freq $freq

		log_must zinject -c all
	done
}

# Disable cache, to ensure reads induce IO
log_must zfs set primarycache=none $TESTPOOL

# Test 'em all.
log_must test_device_injection
log_must test_object_injection
log_must test_delay_injection

log_pass "zinject counts are displayed and advanced as expected."
