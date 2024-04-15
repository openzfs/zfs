#!/bin/ksh -p
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
#

#
# TODO: this only checks that the set of valid device fault types. It should
#       check all the other options, and that they work, and everything really.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

log_assert "Check zinject parameters."

log_onexit cleanup

DISK1=${DISKS%% *}

function cleanup
{
	zinject -c all
	default_cleanup_noexit
}

function test_device_fault
{
	typeset -a errno=("io" "decompress" "decrypt" "nxio" "dtl" "corrupt" "noop")
	for e in ${errno[@]}; do
		log_must eval \
		    "zinject -d $DISK1 -e $e -T read -f 0.001 $TESTPOOL"
	done
	zinject -c all
}

default_mirror_setup_noexit $DISKS

test_device_fault

log_pass "zinject parameters work as expected."
