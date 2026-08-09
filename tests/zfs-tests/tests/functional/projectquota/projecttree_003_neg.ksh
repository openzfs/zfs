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
# Copyright (c) 2017 by Fan Yong. All rights reserved.
#

. $STF_SUITE/tests/functional/projectquota/projectquota_common.kshlib

#
#
# DESCRIPTION:
#	Check 'zfs project' invalid options combinations
#
#
# STRATEGY:
#	Verify the following:
#	1. "-c" only supports "-d", "-p", "-r" and "-0".
#	2. "-C" only supports "-r" and "-k".
#	3. "-s" only supports "-r" and "-p".
#	4. "-c", "-C" and "-s" can NOT be specified together.
#	5. "-d" can overwrite former "-r".
#	6. "-r" can overwrite former "-d".
#	7. "-0" must be together with "-c".
#	8. "-d" must be on directory.
#	9. "-r" must be on directory.
#	10. "-p" must be together with "-c -r" or "-s".
#

function cleanup
{
	log_must rm -rf $PRJDIR
}

log_onexit cleanup

log_assert "Check 'zfs project' invalid options combinations"

log_must mkdir $PRJDIR
log_must mkdir $PRJDIR/a1
log_must touch $PRJDIR/a2

log_mustnot zfs project -c
log_mustnot zfs project -c -k $PRJDIR/a1
log_mustnot zfs project -c -C $PRJDIR/a1
log_mustnot zfs project -c -s $PRJDIR/a1
log_must zfs project -c -d -r $PRJDIR/a1
log_must zfs project -c -r -d $PRJDIR/a1
log_mustnot zfs project -c -d $PRJDIR/a2
log_mustnot zfs project -c -r $PRJDIR/a2

log_mustnot zfs project -C
log_mustnot zfs project -C -c $PRJDIR/a1
log_mustnot zfs project -C -d $PRJDIR/a1
log_mustnot zfs project -C -p 100 $PRJDIR/a1
log_mustnot zfs project -C -s $PRJDIR/a1
log_mustnot zfs project -C -r -0 $PRJDIR/a1
log_mustnot zfs project -C -0 $PRJDIR/a1

log_mustnot zfs project -s
log_mustnot zfs project -s -d $PRJDIR/a1
log_mustnot zfs project -s -k $PRJDIR/a1
log_mustnot zfs project -s -r -0 $PRJDIR/a1
log_mustnot zfs project -s -0 $PRJDIR/a1
log_mustnot zfs project -s -r $PRJDIR/a2

log_mustnot zfs project -p 100
log_mustnot zfs project -p -1 $PRJDIR/a2
log_mustnot zfs project -p 100 -d $PRJDIR/a1
log_mustnot zfs project -p 100 -k $PRJDIR/a1
log_mustnot zfs project -p 100 -0 $PRJDIR/a1
log_mustnot zfs project -p 100 -r -0 $PRJDIR/a1

log_mustnot zfs project
log_mustnot zfs project -0 $PRJDIR/a2
log_mustnot zfs project -k $PRJDIR/a2
log_mustnot zfs project -S $PRJDIR/a1

log_pass "Check 'zfs project' invalid options combinations"
