#!/bin/ksh

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
# Copyright (c) 2015 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

[[ -z $FIO ]] && log_fail "Missing fio"
[[ -z $FREE ]] && log_fail "Missing free"
[[ -z $IOSTAT ]] && log_fail "Missing iostat"
[[ -z $LSBLK ]] && log_fail "Missing lsblk"
[[ -z $MPSTAT ]] && log_fail "Missing mpstat"
[[ -z $VMSTAT ]] && log_fail "Missing vmstat"

verify_runnable "global"
verify_disk_count "$DISKS" 3

log_pass
