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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_get/zfs_get_list_d.kshlib

#
# DESCRIPTION:
# Executing well-formed 'zfs list' commands should return success.
#
# STRATEGY:
# 1. Create an array of valid options.
# 2. Execute each element in the array.
# 3. Verify success is returned.
#

verify_runnable "both"

set -A args "list" "list -r" "list -H" \
        "list $TESTPOOL/$TESTFS" \
        "list -r $TESTPOOL/$TESTFS" "list -H $TESTPOOL/$TESTFS" \
        "list -rH $TESTPOOL/$TESTFS" "list -Hr $TESTPOOL/$TESTFS" \
        "list -o name $TESTPOOL/$TESTFS" "list -r -o name $TESTPOOL/$TESTFS" \
        "list -H -o name $TESTPOOL/$TESTFS" "list -rH -o name $TESTPOOL/$TESTFS" \
        "list -Hr -o name $TESTPOOL/$TESTFS"

set -A d_args " " "-r" "-H" \
        "$TESTPOOL/$TESTFS" \
        "-r $TESTPOOL/$TESTFS" "-H $TESTPOOL/$TESTFS" \
        "-rH $TESTPOOL/$TESTFS" "-Hr $TESTPOOL/$TESTFS" \
        "-o name $TESTPOOL/$TESTFS" "-r -o name $TESTPOOL/$TESTFS" \
        "-H -o name $TESTPOOL/$TESTFS" "-rH -o name $TESTPOOL/$TESTFS" \
        "-Hr -o name $TESTPOOL/$TESTFS"

typeset -i m=${#args[*]}
typeset -i n=0
typeset -i k=0
while (( n<${#depth_options[*]} ));
do
	(( k=0 ))
	while (( k<${#d_args[*]} ));
	do
		args[$m]="list"" -${depth_options[$n]}"" ${d_args[$k]}"
		(( k+=1 ))
		(( m+=1 ))
	done
	(( n+=1 ))
done

set -A pathargs "list -r $TESTDIR" "list -H $TESTDIR" \
	"list -r ./../$TESTDIR" "list -H ./../$TESTDIR"

set -A d_pathargs " $TESTDIR" "-r $TESTDIR" "-H $TESTDIR" \
	"-r ./../$TESTDIR" "-H ./../$TESTDIR"

(( m=${#pathargs[*]} ))
(( n=0 ))
(( k=0 ))
while (( n<${#depth_options[*]} ));
do
	(( k=0 ))
	while (( k<${#d_pathargs[*]} ));
	do
		pathargs[$m]="list"" -${depth_options[$n]}"" ${d_pathargs[$k]}"
		(( k+=1 ))
		(( m+=1 ))
	done
	(( n+=1 ))
done

log_assert "Verify 'zfs list [-rH] [-o property[,prop]*] [fs|clct|vol]'."

typeset -i i=0
while [[ $i -lt ${#args[*]} ]]; do
	log_must eval "zfs ${args[i]} > /dev/null"
	((i = i + 1))
done

# Verify 'zfs list <path>' will succeed on absolute or relative path.

cd /tmp
typeset -i i=0
while [[ $i -lt ${#pathargs[*]} ]]; do
	log_must eval "zfs ${pathargs[i]} > /dev/null"
	((i = i + 1))
done

log_pass "The sub-command 'list' succeeds as non-root."
