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
# Copyright (c) 2024 The FreeBSD Foundation
#
# This software was developed by Pawel Dawidek <pawel@dawidek.net>
# under sponsorship from the FreeBSD Foundation.
#

. $STF_SUITE/include/libtest.shlib

if ! command -v fsop > /dev/null ; then
	log_unsupported "fsop program required to test iolimit"
fi

DISK=${DISKS%% *}

default_setup_noexit $DISK "true"

# Make the pool as fast as possible, so we don't have tests failing, because
# the test pool is a bit too slow.
log_must zfs set atime=off $TESTPOOL
log_must zfs set checksum=off $TESTPOOL
log_must zfs set compress=zle $TESTPOOL
log_must zfs set recordsize=1M $TESTPOOL
log_must zfs set sync=disabled $TESTPOOL

log_pass
