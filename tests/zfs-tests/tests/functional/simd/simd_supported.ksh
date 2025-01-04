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
# Copyright (c) 2022 by Attila Fülöp <attila@fueloep.org>
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#       Make sure we have SIMD support, so it will not go away without notice
#
# STRATEGY:
#	1. Test if we are running on a Linux x86 system with SSE support
#	2. If so, check if the zfs_fletcher_4_impl module parameter contains
#	   a sse implementation
#	3. If not fail the test, otherwise pass it

log_note "Testing if we support SIMD instructions (Linux x86 only)"

if ! is_linux; then
    log_unsupported "Not a Linux System"
fi

case "$(uname -m)" in
i?86|x86_64)
	typeset -R modparam="/sys/module/zfs/parameters/zfs_fletcher_4_impl"
	if awk '/^flags/ {exit !/sse/}' /proc/cpuinfo; then
		log_must grep -q sse "$modparam"
		log_pass "SIMD instructions supported"
	else
		log_unsupported "No FPU present"
	fi
	;;
*)
	log_unsupported "Not a x86 CPU"
	;;
esac
