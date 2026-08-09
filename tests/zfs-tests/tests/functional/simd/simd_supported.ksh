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
