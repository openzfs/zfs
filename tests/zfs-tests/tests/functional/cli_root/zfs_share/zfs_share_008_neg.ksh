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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that sharing a dataset other than filesystem fails.
#
# STRATEGY:
# 1. Create a ZFS file system.
# 2. For each dataset in the list, set the sharenfs property.
# 3. Verify that the invalid datasets are not shared.
#

verify_runnable "global"

if is_global_zone ; then
	set -A datasets \
	    "$TESTPOOL/$TESTVOL" "$TESTDIR"
fi

log_assert "Verify that sharing a dataset other than filesystem fails."

typeset -i i=0
while (( i < ${#datasets[*]} ))
do
	log_mustnot zfs set sharenfs=on ${datasets[i]}

	option=`get_prop sharenfs ${datasets[i]}`
	if [[ $option == ${datasets[i]} ]]; then
		log_fail "set sharenfs failed. ($option == ${datasets[i]})"
	fi

	not_shared ${datasets[i]} || \
	    log_fail "An invalid setting '$option' was propagated."

	log_mustnot zfs share ${datasets[i]}

	not_shared ${datasets[i]} || \
	    log_fail "An invalid dataset '${datasets[i]}' was shared."

	((i = i + 1))
done

log_pass "Sharing datasets other than filesystems failed as expected."
