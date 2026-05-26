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
# Copyright (c) 2024, Klara Inc.
# Copyright (c) 2026, Christos Longros <chris.longros@gmail.com>
#

#
# This covers device, label, object, delay, panic injection modes:
# every valid value is accepted and unknown values are rejected.
# A final pass also confirms that a registered injection actually
# executes by watching the inject counter advance after triggering
# the desired injected error.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

log_assert "Check zinject parameters."

log_onexit cleanup

DISK1=${DISKS%% *}
TESTFILE=/$TESTPOOL/zinject_args.file

function cleanup
{
	zinject -c all
	default_cleanup_noexit
}

function test_device_fault
{
	typeset -a errno=("io" "decompress" "decrypt" "nxio" "dtl" "corrupt" "noop" "io-prefail")
	for e in ${errno[@]}; do
		log_must eval \
		    "zinject -d $DISK1 -e $e -T read -f 0.001 $TESTPOOL"
	done
	zinject -c all
}

function test_device_fault_neg
{
	log_mustnot eval "zinject -d $DISK1 -e bogus -T read $TESTPOOL"
	log_mustnot eval "zinject -d $DISK1 -e io -T bogus $TESTPOOL"
	zinject -c all
}

function test_label_fault
{
	typeset -a labels=("nvlist" "pad1" "pad2" "uber")
	for l in ${labels[@]}; do
		log_must eval \
		    "zinject -d $DISK1 -e io -L $l $TESTPOOL"
	done
	zinject -c all
}

function test_label_fault_neg
{
	log_mustnot eval "zinject -d $DISK1 -e io -L bogus $TESTPOOL"
	zinject -c all
}

function test_object_fault
{
	log_must dd if=/dev/urandom of=$TESTFILE bs=128k count=1
	log_must zpool sync $TESTPOOL

	for t in data dnode; do
		log_must eval "zinject -t $t -e io -f 0.001 $TESTFILE"
	done
	zinject -c all

	for t in mos mosdir metaslab config bpobj spacemap errlog; do
		log_must eval "zinject -t $t -e io -f 0.001 $TESTPOOL"
	done
	zinject -c all
}

function test_object_fault_neg
{
	log_mustnot eval "zinject -t bogus -e io $TESTPOOL"
	log_mustnot eval "zinject -t data -e bogus $TESTFILE"
	# -t data only accepts checksum or io as the error type.
	log_mustnot eval "zinject -t data -e nxio $TESTFILE"
	zinject -c all
}

function test_delay_fault
{
	log_must eval "zinject -d $DISK1 -D 10:1 $TESTPOOL"
	log_must eval "zinject -d $DISK1 -D 25:2 -T read $TESTPOOL"
	log_must eval "zinject -d $DISK1 -D 25:2 -T write $TESTPOOL"
	zinject -c all
}

function test_delay_fault_neg
{
	log_mustnot eval "zinject -d $DISK1 -D 0:1 $TESTPOOL"
	log_mustnot eval "zinject -d $DISK1 -D 10 $TESTPOOL"
	log_mustnot eval "zinject -d $DISK1 -D foo $TESTPOOL"
	zinject -c all
}

function test_panic_fault
{
	# An unmatched function tag so zio_handle_panic_injection() never fires.
	log_must eval "zinject -p zfs_test_no_such_fn $TESTPOOL"
	log_must eval "zinject -p zfs_test_no_such_fn $TESTPOOL 1"
	zinject | grep -q zfs_test_no_such_fn || \
	    log_fail "panic function was not registered"
	zinject -c all
}

function test_panic_fault_neg
{
	log_mustnot eval "zinject -p f -d $DISK1 $TESTPOOL"
	log_mustnot eval "zinject -p f -t data $TESTFILE"
	log_mustnot eval "zinject -p f -f 50 $TESTPOOL"
	zinject -c all
}

# Each registered device/delay/data handler row ends with "match inject".
function inject_count
{
	zinject | awk '/^ *[0-9]/{print $NF}' | head -n 1
}

function verify_injection
{
	typeset cnt

	log_must zfs set primarycache=none $TESTPOOL
	log_must dd if=/dev/urandom of=$TESTFILE bs=128k count=1
	log_must zpool sync $TESTPOOL

	log_must eval "zinject -d $DISK1 -e io -T read -f 100 $TESTPOOL"
	dd if=$TESTFILE of=/dev/null bs=128k count=1 >/dev/null 2>&1 || true
	cnt=$(inject_count)
	[[ -n $cnt && $cnt -gt 0 ]] || \
	    log_fail "device-fault injection did not execute (inject=$cnt)"
	zinject -c all

	log_must eval "zinject -t data -e checksum -f 100 $TESTFILE"
	dd if=$TESTFILE of=/dev/null bs=128k count=1 >/dev/null 2>&1 || true
	cnt=$(inject_count)
	[[ -n $cnt && $cnt -gt 0 ]] || \
	    log_fail "object-fault injection did not execute (inject=$cnt)"
	zinject -c all

	log_must eval "zinject -d $DISK1 -D 5:1 -T write $TESTPOOL"
	log_must dd if=/dev/urandom of=$TESTFILE bs=128k count=1
	log_must zpool sync $TESTPOOL
	cnt=$(inject_count)
	[[ -n $cnt && $cnt -gt 0 ]] || \
	    log_fail "delay injection did not execute (inject=$cnt)"
	zinject -c all

	log_must zfs inherit primarycache $TESTPOOL
}

default_mirror_setup_noexit $DISKS

test_device_fault
test_device_fault_neg
test_label_fault
test_label_fault_neg
test_object_fault
test_object_fault_neg
test_delay_fault
test_delay_fault_neg
test_panic_fault
test_panic_fault_neg
verify_injection

log_pass "zinject parameters work as expected."
