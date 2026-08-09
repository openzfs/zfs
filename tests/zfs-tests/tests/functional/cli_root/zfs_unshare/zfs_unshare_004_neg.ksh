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
