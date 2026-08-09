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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
# the zfs rootfilesystem's compression property can be set to gzip[1-9]
#
# STRATEGY:
# 1) check if the current system is installed as zfs root
# 2) get the rootfs
# 3) set the rootfs's compression to gzip 1-9 which should not fail.
#

verify_runnable "global"

function cleanup {
	[[ -n "$orig_compress" ]] && \
	    log_must zfs set compression=$orig_compress $rootfs
}

typeset assert_msg="the zfs rootfs's compression property can be set to \
		   gzip and gzip[1-9]"

log_onexit cleanup
log_assert $assert_msg

typeset rootpool=$(get_rootpool)
typeset rootfs=$(get_pool_prop bootfs $rootpool)
typeset orig_compress=$(get_prop compression $rootfs)

set -A gtype "gzip" "gzip-1" "gzip-2" "gzip-3" "gzip-4" "gzip-5" \
	     "gzip-6" "gzip-7" "gzip-8" "gzip-9"

typeset -i i=0
while (( i < ${#gtype[@]} )); do
	log_must zfs set compression=${gtype[i]} $rootfs
	(( i += 1 ))
done

log_pass $assert_msg
