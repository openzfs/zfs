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

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

verify_runnable "both"

if is_global_zone ; then
	destroy_pool $POOL
	destroy_pool $POOL2
	poolexists $POOL3 && destroy_pool $POOL3
else
	cleanup_pool $POOL
	cleanup_pool $POOL2
	poolexists $POOL3 && cleanup_pool $POOL3
fi
log_must rm -rf $BACKDIR $TESTDIR

log_pass
