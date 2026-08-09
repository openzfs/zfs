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
# Copyright (c) 2018 George Melikov. All Rights Reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/delegate/delegate_common.kshlib

cleanup_user_group

if ! is_linux; then
	# restore the state of svc:/network/nis/client:default
	if [[ -e $NISSTAFILE ]]; then
		log_must svcadm enable svc:/network/nis/client:default
		log_must rm -f $NISSTAFILE
	fi
fi

if is_freebsd; then
	log_must sysctl vfs.usermount=0
fi

if is_linux; then
	log_must set_tunable64 ADMIN_SNAPSHOT 0
fi

default_cleanup
