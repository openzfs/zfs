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
# Executing 'zpool import' as regular user should denied.
#
# STRATEGY:
# 1. Create an array of options try to detect exported/destroyed pools.
# 2. Execute 'zpool import' with each element of the array by regular user.
# 3. Verify an error code is returned.
#

verify_runnable "both"

typeset testpool
if is_global_zone ; then
        testpool=$TESTPOOL.exported
else
        testpool=${TESTPOOL%%/*}
fi

set -A args "" "-D" "-Df" "-f" "-f $TESTPOOL" "-Df $TESTPOOL" "-a"

log_assert "Executing 'zpool import' by regular user fails"

typeset -i i=0
while [[ $i -lt ${#args[*]} ]]; do
	log_mustnot zpool import ${args[i]}
	((i = i + 1))
done

log_pass "Executing 'zpool import' by regular user fails as expected."
