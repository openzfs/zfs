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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_user/misc/misc.cfg

#
# DESCRIPTION:
# Verify that 'zfs destroy' fails as non-root.
#
# STRATEGY:
# 1. Create an array of options.
# 2. Execute each element of the array.
# 3. Verify an error code is returned.
#

verify_runnable "both"


set -A args "destroy" "destroy $TESTPOOL/$TESTFS" \
    "destroy -f" "destroy -f $TESTPOOL/$TESTFS" \
    "destroy -r" "destroy -r $TESTPOOL/$TESTFS" \
    "destroy -rf $TESTPOOL/$TESTFS" \
    "destroy -fr $TESTPOOL/$TESTFS" \
    "destroy $TESTPOOL/$TESTFS@$TESTSNAP" \
    "destroy -f $TESTPOOL/$TESTFS@$TESTSNAP" \
    "destroy -r $TESTPOOL/$TESTFS@$TESTSNAP" \
    "destroy -rf $TESTPOOL/$TESTFS@$TESTSNAP" \
    "destroy -fr $TESTPOOL/$TESTFS@$TESTSNAP"

log_assert "zfs destroy [-f|-r] [fs|snap]"

typeset -i i=0
while [[ $i -lt ${#args[*]} ]]; do
	log_mustnot zfs ${args[i]}
	((i = i + 1))
done

log_pass "The sub-command 'destroy' fails as non-root."
