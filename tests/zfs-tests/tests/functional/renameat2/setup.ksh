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
# Copyright (C) 2019 Aleksa Sarai <cyphar@cyphar.com>
# Copyright (C) 2019 SUSE LLC
#

. $STF_SUITE/include/libtest.shlib

if ! is_linux ; then
	log_unsupported "renameat2 is linux-only"
elif ! renameat2 -C ; then
	log_unsupported "renameat2 not supported on this (pre-3.15) linux kernel"
fi

DISK=${DISKS%% *}
default_setup $DISK
