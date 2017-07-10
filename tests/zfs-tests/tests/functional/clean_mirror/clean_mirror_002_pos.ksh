#! /bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
# The secondary side of a zpool mirror can be zeroed without causing damage
# to the data in the pool
#
# STRATEGY:
# 1) Write several files to the ZFS filesystem in the mirrored pool
# 2) dd from /dev/zero over the secondary side of the mirror
# 3) verify that all the file contents are unchanged on the file system
#

verify_runnable "global"

log_assert "The primary side of a zpool mirror may be completely wiped" \
	"without affecting the content of the pool"

overwrite_verify_mirror $SIDE_SECONDARY /dev/zero

log_pass "The overwrite had no effect on the data"
