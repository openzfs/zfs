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
