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

. $STF_SUITE/tests/functional/cli_root/zfs_get/zfs_get_common.kshlib

#
# DESCRIPTION:
# 'zfs get -o' should fail with invalid column names
#
# STRATEGY:
# 1. Run zfs get -o with invalid column name combinations
# 2. Verify that zfs get returns error
#

verify_runnable "both"

log_assert "'zfs get -o' fails with invalid options or column names"

set -A  badargs "o name,property,value,resource" "o name" \
	"-O name,property,value,source" "-oo name" "-o blah" \
	"-o name,property,blah,source" "-o name,name,name,name,name" \
	"-o name,property,value,," "-o *,*,*,*" "-o ?,?,?,?" \
	"-o" "-o ,,,,," "-o -o -o -o" "-o NAME,PROPERTY,VALUE,SOURCE" \
	"-o name,properTy,value,source" "-o name, property, value,source" \
	"-o name:property:value:source" "-o name,property:value,source" \
	"-o name;property;value;source" "-o name=getsubopt"

typeset -i i=0
while (( i < ${#badargs[*]} ))
do
	log_mustnot eval "zfs get \"${badargs[i]}\" >/dev/null 2>&1"

	(( i = i + 1 ))
done

log_pass "'zfs get -o' fails with invalid options or column name as expected."
