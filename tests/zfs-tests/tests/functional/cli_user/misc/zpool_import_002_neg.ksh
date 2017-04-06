#!/bin/ksh -p
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
