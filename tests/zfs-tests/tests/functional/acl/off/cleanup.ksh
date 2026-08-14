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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/acl/acl_common.kshlib

# posixmode can leave mode-007 trees; do not sit on that mount.
cd / || true
if [[ -n $TESTDIR && $TESTDIR != / && -d $TESTDIR ]]; then
	chmod -R u+rwx "$TESTDIR" >/dev/null 2>&1 || true
	rm -rf "$TESTDIR"/* >/dev/null 2>&1 || true
fi

cleanup_user_group

default_cleanup
