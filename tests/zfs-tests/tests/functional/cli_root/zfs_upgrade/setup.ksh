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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "both"

# This should have been set by the .cfg script - verify it's set to something
# (we check that something later on)
if [ -z "$ZFS_VERSION" ]
then
   log_unresolved "Unable to determine ZFS Filesystem version of this machine"
else
   log_note "This machine is running ZFS Filesystem version $ZFS_VERSION"
fi

default_setup "$DISKS"
