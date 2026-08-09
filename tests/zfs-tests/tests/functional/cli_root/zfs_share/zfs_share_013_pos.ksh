#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

#
# Copyright (c) 2020, Felix Dörre
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that NFS share options including ipv6 literals are parsed and propagated correctly.
#

verify_runnable "global"

function cleanup
{
	log_must zfs set sharenfs=off $TESTPOOL/$TESTFS
	is_shared $TESTPOOL/$TESTFS && \
		log_must unshare_fs $TESTPOOL/$TESTFS
}

log_onexit cleanup

cleanup

log_must zfs set sharenfs="rw=[::1]" $TESTPOOL/$TESTFS
output=$(showshares_nfs 2>&1)
log_must grep -q "::1(" <<< "$output"

log_must zfs set sharenfs="rw=[2::3]" $TESTPOOL/$TESTFS
output=$(showshares_nfs 2>&1)
log_must grep -q "2::3(" <<< "$output"

log_must zfs set sharenfs="rw=[::1]:[2::3]" $TESTPOOL/$TESTFS
output=$(showshares_nfs 2>&1)
log_must grep -q "::1(" <<< "$output"
log_must grep -q "2::3(" <<< "$output"

log_must zfs set sharenfs="rw=[::1]/64" $TESTPOOL/$TESTFS
output=$(showshares_nfs 2>&1)
log_must grep -q "::1/64(" <<< "$output"

log_must zfs set sharenfs="rw=[2::3]/128" $TESTPOOL/$TESTFS
output=$(showshares_nfs 2>&1)
log_must grep -q "2::3/128(" <<< "$output"

log_must zfs set sharenfs="rw=[::1]/32:[2::3]/128" $TESTPOOL/$TESTFS
output=$(showshares_nfs 2>&1)
log_must grep -q "::1/32(" <<< "$output"
log_must grep -q "2::3/128(" <<< "$output"

log_must zfs set sharenfs="rw=[::1]:[2::3]/64:[2a01:1234:1234:1234:aa34:234:1234:1234]:1.2.3.4/24" $TESTPOOL/$TESTFS
output=$(showshares_nfs 2>&1)
log_must grep -q "::1(" <<< "$output"
log_must grep -q "2::3/64(" <<< "$output"
log_must grep -q "2a01:1234:1234:1234:aa34:234:1234:1234(" <<< "$output"
log_must grep -q "1\\.2\\.3\\.4/24(" <<< "$output"

log_pass "NFS share ip address propagated correctly."
