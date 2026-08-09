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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

#
# The pool expansion tests depend on udev change events being generated
# when block devices change capacity.  Since this functionality wasn't
# available until the 2.6.38 kernel skip this test group.
#
if [[ $(linux_version) -lt $(linux_version "2.6.38") ]]; then
	log_unsupported "Requires block device udev change events"
fi

zed_setup
zed_start

DISK=${DISKS%% *}

default_setup $DISK
