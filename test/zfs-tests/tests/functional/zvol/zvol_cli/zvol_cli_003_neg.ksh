#! /usr/bin/ksh -p
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

#
# DESCRIPTION:
# Try each ZFS volume sub-command without parameters to make sure
# it returns an error.
#
# STRATEGY:
# 1. Create an array of parameters
# 2. For each parameter in the array, execute the sub-command
# 3. Verify an error is returned.
#

verify_runnable "global"

set -A args "" "create -V" "create -V $TESTPOOL" \
	"create -V $TESTPOOL/$TESTVOL@" "create -V blah" "destroy"

log_assert "Try each ZFS volume sub-command without parameters to make sure" \
    " it returns an error."

typeset -i i=0
while (( $i < ${#args[*]} )); do
	log_mustnot $ZFS ${args[i]}
	(( i = i + 1 ))
done

log_pass "Badly formed ZFS volume sub-commands fail as expected."
