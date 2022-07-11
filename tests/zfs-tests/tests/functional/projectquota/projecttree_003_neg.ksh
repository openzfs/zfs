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
