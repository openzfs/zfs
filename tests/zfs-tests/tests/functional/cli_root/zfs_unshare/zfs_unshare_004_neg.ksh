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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that "zfs unshare" issue error message with badly formed parameter.
#
# STRATEGY:
# 1. Define badly formed parameters
# 2. Invoke 'zfs unshare'
# 3. Verify that unshare fails and issue error message.
#

verify_runnable "global"

export NONEXISTFSNAME="nonexistfs50charslong_0123456789012345678901234567"
export NONEXISTMOUNTPOINT="/nonexistmountpoint_0123456789"

set -A opts "" "$TESTPOOL/$NONEXISTFSNAME" "$NONEXISTMOUNTPOINT" "-?" "-1" \
		"-a blah" "$TESTPOOL/$TESTFS $TESTPOOL/$TESTFS1" \
		"-f $TESTPOOL/$TESTFS $TESTPOOL/$TESTFS1" \
		"$TESTPOOL/$TESTFS $TESTDIR" "-f $TESTPOOL/$TESTFS $TESTDIR" \
		"${TESTDIR#/}" "-f ${TESTDIR#/}"

log_assert "Verify that 'zfs unshare' issue error message with badly formed parameter."

shareval=$(get_prop sharenfs $TESTPOOL/$TESTFS)
if [[ $shareval == off ]]; then
	log_must zfs set sharenfs=on $TESTPOOL/$TESTFS
fi

typeset -i i=0
while [[ $i -lt ${#args[*]} ]]; do
        log_mustnot zfs unshare ${args[i]}

        ((i = i + 1))
done

#Testing that unsharing unshared filesystem fails.
mpt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
log_must zfs unshare $TESTPOOL/$TESTFS
for opt in "" "-f"; do
	log_mustnot eval "zfs unshare $opt $TESTPOOL/$TESTFS >/dev/null 2>&1"
	log_mustnot eval "zfs unshare $opt $mpt >/dev/null 2>&1"
done

#Testing zfs unshare fails with legacy share set
log_must zfs set sharenfs=off $TESTPOOL/$TESTFS
for opt in "" "-f"; do
	log_mustnot eval "zfs unshare $opt $TESTPOOL/$TESTFS >/dev/null 2>&1"
	log_mustnot eval "zfs unshare $opt $mpt >/dev/null 2>&1"
done

log_pass "'zfs unshare' fails as expected with badly-formed parameters."
