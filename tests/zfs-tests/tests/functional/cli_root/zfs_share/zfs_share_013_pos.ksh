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
