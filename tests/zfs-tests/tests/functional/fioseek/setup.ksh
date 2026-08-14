#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

if ! is_netbsd; then
	log_unsupported "FIOSEEKHOLE/FIOSEEKDATA is a NetBSD file ioctl"
fi

disk=${DISKS%% *}
default_setup $disk
