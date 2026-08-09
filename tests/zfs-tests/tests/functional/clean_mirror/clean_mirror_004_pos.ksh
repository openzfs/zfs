#! /bin/ksh -p
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
# Copyright (c) 2013 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/clean_mirror/clean_mirror_common.kshlib

#
# DESCRIPTION:
# The secondary side of a zpool mirror can be mangled without causing damage
# to the data in the pool
#
# STRATEGY:
# 1) Write several files to the ZFS filesystem in the mirrored pool
# 2) dd from /dev/urandom over the secondary side of the mirror
# 3) verify that all the file contents are unchanged on the file system
#

verify_runnable "global"

log_assert "The primary side of a zpool mirror may be completely mangled" \
	"without affecting the content of the pool"

overwrite_verify_mirror $SIDE_SECONDARY /dev/urandom

log_pass "The overwrite had no effect on the data"
