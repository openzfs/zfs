#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
#!/bin/ksh -p
#
# CDDL HEADER START
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
# CDDL HEADER END
#

#
# Copyright (c) 2019 by Tim Chase. All rights reserved.
# Copyright (c) 2019 Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

if poolexists $TESTPOOL; then
	destroy_pool $TESTPOOL
fi

if poolexists $TESTPOOL1; then
	destroy_pool $TESTPOOL1
fi

TRIM_DIR="$TEST_BASE_DIR"
TRIM_VDEVS="$TRIM_DIR/trim-vdev1 $TRIM_DIR/trim-vdev2 \
    $TRIM_DIR/trim-vdev3 $TRIM_DIR/trim-vdev4"

rm -rf $TRIM_VDEVS

default_cleanup
