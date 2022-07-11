#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_user/misc/misc.cfg

#
# DESCRIPTION:
# Verify that 'zpool create' fails as a non-root user.
#
# STRATEGY:
# 1. Create an array of options.
# 2. Execute each element of the array.
# 3. Verify that an error is returned.
#

verify_runnable "global"

ADD_DISK="${DISKS%% }"
ADD_DISK="${ADD_DISK##* }"

[[ -z $ADD_DISK ]] && \
        log_fail "No spare disks available."

# Under Linux dry-run commands have no legitimate reason to fail.
if is_linux; then
	set -A args "create" "create -f" "create -n" \
	    "create $TESTPOOL" "create -f $TESTPOOL" "create -n $TESTPOOL" \
	    "create -fn $TESTPOOL" "create -nf $TESTPOOL" \
	    "create $TESTPOOL $ADD_DISK" "create -f $TESTPOOL $ADD_DISK"
else
	set -A args "create" "create -f" "create -n" \
	    "create $TESTPOOL" "create -f $TESTPOOL" "create -n $TESTPOOL" \
	    "create -fn $TESTPOOL" "create -nf $TESTPOOL" \
	    "create $TESTPOOL $ADD_DISK" "create -f $TESTPOOL $ADD_DISK" \
	    "create -n $TESTPOOL $ADD_DISK" \
	    "create -fn $TESTPOOL $ADD_DISK" \
	    "create -nf $TESTPOOL $ADD_DISK"
fi

log_assert "zpool create [-fn] pool_name vdev"

typeset -i i=0
while [[ $i -lt ${#args[*]} ]]; do
	log_mustnot zpool ${args[i]}
	((i = i + 1))
done

log_pass "The sub-command 'create' and its options fail as non-root."
