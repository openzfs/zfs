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

. $STF_SUITE/include/libtest.shlib

DISK=${DISKS%% *}

TESTPOOLDISK=${DISKS%% *}
TESTPOOL2DISK=${DISKS##* }

if [[ "$(uname -m)" == "aarch64" ]]; then
	if is_linux && awk '/^CPU implementer/ {exit !/0x61/}' /proc/cpuinfo; then
		log_unsupported "Not supported on Apple Silicon"
	elif is_freebsd && sysctl machdep.cpu.brand_string | grep -q Apple; then
		log_unsupported "Not supported on Apple Silicon"
	fi
fi

default_setup ${TESTPOOLDISK}
create_pool testpool2 ${TESTPOOL2DISK}
