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
#	Check project ID/flag can be operated via "zfs project"
#
#
# STRATEGY:
#	1. Create a tree with 4 level directories.
#	2. Set project ID on both directory and regular file via
#	   "zfs project -p".
#	3. Check the project ID via "zfs project".
#	4. Set project inherit flag on kinds of level directories (and its
#	   descendants for some)) via "zfs project -s [-r]".
#	5. Check the project ID and inherit flag via "zfs project -r".
#	6. Clear the project inherit flag from some directories (and its
#	   descendants for some) via "zfs project -C [-r]".
#	7. Check the project ID and inherit flag via "zfs project -r".
#

function cleanup
{
	log_must rm -rf $PRJDIR
}

log_onexit cleanup

log_assert "Check project ID/flag can be operated via 'zfs project'"

log_must mkdir $PRJDIR

log_must mkdir $PRJDIR/a1
log_must mkdir $PRJDIR/b1
log_must touch $PRJDIR/c1

log_must mkdir $PRJDIR/a1/a2
log_must mkdir $PRJDIR/a1/b2
log_must touch $PRJDIR/a1/c2

log_must mkdir $PRJDIR/b1/a2
log_must mkdir $PRJDIR/b1/b2
log_must touch $PRJDIR/b1/c2

log_must mkdir $PRJDIR/a1/a2/a3
log_must mkdir $PRJDIR/a1/a2/b3
log_must touch $PRJDIR/a1/a2/c3

log_must mkdir $PRJDIR/b1/a2/a3

log_must touch $PRJDIR/a1/a2/a3/c4
log_must touch $PRJDIR/a1/a2/a3/d4

log_must zfs project -p $PRJID1 $PRJDIR/a1/c2
log_must eval "zfs project $PRJDIR/a1/c2 | grep $PRJID1"

log_must zfs project -p $PRJID2 $PRJDIR/a1/a2/a3
log_must eval "zfs project -d $PRJDIR/a1/a2/a3 | grep $PRJID2"

log_must zfs project -s $PRJDIR/b1/a2
log_must eval "zfs project -d $PRJDIR/b1/a2 | grep ' P '"
log_must eval "zfs project -d $PRJDIR/b1/a2/a3 | grep ' \- '"

log_must zfs project -s -r -p $PRJID2 $PRJDIR/a1/a2
log_must zfs project -c -r $PRJDIR/a1/a2
log_must eval "zfs project -d $PRJDIR/a1/a2/a3 | grep ' P '"
log_must eval "zfs project $PRJDIR/a1/a2/a3/c4 | grep $PRJID2"

log_must zfs project -C $PRJDIR/a1/a2/a3
log_must eval "zfs project -cr $PRJDIR/a1/a2 | grep 'inherit flag is not set'"
log_must eval "zfs project $PRJDIR/a1/a2/a3/c4 | grep $PRJID2 | grep -v not"
log_must zfs project -p 123 $PRJDIR/a1/a2/a3/c4
log_must eval "zfs project -c -r $PRJDIR/a1/a2 | grep 123 | grep 'not set'"
log_mustnot eval "zfs project -cr -p 123 $PRJDIR/a1/a2 | grep c4 | grep -v not"

log_must zfs project -C -r $PRJDIR/a1/a2/a3
log_must eval "zfs project -cr $PRJDIR/a1/a2 | grep a3 | grep 'not set'"
log_must eval "zfs project -cr $PRJDIR/a1/a2 | grep d4 | grep 'not set'"
log_must eval "zfs project $PRJDIR/a1/a2/a3/d4 | grep '0 \-'"

log_must eval \
    "zfs project -cr -0 $PRJDIR/a1/a2 | xargs -0 zfs project -s -p $PRJID2"
log_mustnot eval "zfs project -cr $PRJDIR/a1/a2 | grep a3 | grep 'not set'"
log_mustnot eval "zfs project -cr $PRJDIR/a1/a2 | grep d4 | grep 'not set'"

log_must zfs project -C -r -k $PRJDIR/a1/a2
log_must eval "zfs project -d $PRJDIR/a1/a2/b3 | grep '$PRJID2 \- '"

log_pass "Check project ID/flag can be operated via 'zfs project'"
