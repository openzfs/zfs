#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright (c) 2025 by Nutanix. All rights reserved.
#

. $STF_SUITE/tests/functional/projectquota/projectquota_common.kshlib

#
# DESCRIPTION:
#	Validate Hierarchical project quota
#
#
# STRATEGY:
#	1. Setup directory tree
#		a (Directory)
#		 |__ f1 (100M file)
#		 |__ b (Directory)
#		      |__ f1 (100M file)
#		      |__ c (Directory)
#			   |__f1 (100M file)
#
#	2. set 400M projectquota on directory 'a', 'b' and 'c'
#	3. Set a PRJID1 project on on 'a' directory tree
#	4. Validate usage of PRJID1 is 300M
#	5. Set a PRJID2 project on on 'a/b' directory tree
#	6. Validate usage of PRJID1 is 300M
#	7. Validate usage of PRJID2 is 200M
#	8. Set a PRJID3 project on on 'a/b/c' directory tree
#	9. Validate usage of PRJID1 is 300M
#	10. Validate usage of PRJID2 is 200M
#	11. Validate usage of PRJID3 is 100M
#	12. Write 100M data file in 'c'
#	13. Validate usage of PRJID1 is 400M
#	14. Validate usage of PRJID2 is 300M
#	15. Validate usage of PRJID3 is 200M
#	16. Validate new data writes in 'c' fails, as 400M quota on PRJID1 reached
#	17. Validate new data writes in 'b' fails, as 400M quota on PRJID1 reached
#	18. Validate new data writes in 'a' fails, as 400M quota on PRJID1 reached
#	19. Validate same with hierarchy assocation sequence of /a/b/c, /a/b, /a
#	20. Validate same with hierarchy assocation sequence of /a, /a/b/c, /a/b
#	21. Validate removing project quota in middle of hierarchy and adding new project quota
#

function cleanup
{
	log_must cleanup_projectquota
}

log_onexit cleanup

log_assert "Validate Hierarchical project quota"

mkmount_writable $QFS
log_note "Setup Directory tree"
log_must user_run $PUSER mkdir -p $PRJDIR/a/b/c
log_must user_run $PUSER mkfile 100m $PRJDIR/a/f1
log_must user_run $PUSER mkfile 100m $PRJDIR/a/b/f1
log_must user_run $PUSER mkfile 100m $PRJDIR/a/b/c/f1
sync_all_pools
zfs projectspace $QFS

log_note "Validate project assocation sequence /a, /a/b, /a/b/c"
log_mustnot zfs project -p $PRJID1 -srY $PRJDIR/a
log_must zfs set projectquota@$PRJID1=400m $QFS
log_must zfs project -p $PRJID1 -srY $PRJDIR/a
sync_all_pools
zfs projectspace $QFS
log_must eval "zfs projectspace -H -oname,quota $QFS | grep $PRJID1 | grep 400M"
log_must eval "zfs get -H -ovalue projectquota@$PRJID1 $QFS | grep 400M"
log_must eval "zfs projectspace -H -oname,used $QFS | grep $PRJID1 | grep 300\\.\*M"
log_must eval "zfs get -H -ovalue projectused@$PRJID1 $QFS | grep 300\\.\*M"
log_mustnot zfs project -p $PRJID2 -srY $PRJDIR/a/b
log_must zfs set projectquota@$PRJID2=400m $QFS
log_must zfs project -p $PRJID2 -srY $PRJDIR/a/b
sync_all_pools
zfs projectspace $QFS
log_must eval "zfs projectspace -H -oname,quota $QFS | grep $PRJID2 | grep 400M"
log_must eval "zfs get -H -ovalue projectquota@$PRJID2 $QFS | grep 400M"
log_must eval "zfs projectspace -H -oname,used $QFS | grep $PRJID1 | grep 300\\.\*M"
log_must eval "zfs get -H -ovalue projectused@$PRJID1 $QFS | grep 300\\.\*M"
log_must eval "zfs projectspace -H -oname,used $QFS | grep $PRJID2 | grep 200\\.\*M"
log_must eval "zfs get -H -ovalue projectused@$PRJID2 $QFS | grep 200\\.\*M"
log_mustnot zfs project -p $PRJID3 -srY $PRJDIR/a/b/c
log_must zfs set projectquota@$PRJID3=400m $QFS
log_must zfs project -p $PRJID3 -srY $PRJDIR/a/b/c
sync_all_pools
zfs projectspace $QFS
log_must eval "zfs projectspace -H -oname,quota $QFS | grep $PRJID3 | grep 400M"
log_must eval "zfs get -H -ovalue projectquota@$PRJID3 $QFS | grep 400M"
log_must eval "zfs projectspace -H -oname,used $QFS | grep $PRJID1 | grep 300\\.\*M"
log_must eval "zfs get -H -ovalue projectused@$PRJID1 $QFS | grep 300\\.\*M"
log_must eval "zfs projectspace -H -oname,used $QFS | grep $PRJID2 | grep 200\\.\*M"
log_must eval "zfs get -H -ovalue projectused@$PRJID2 $QFS | grep 200\\.\*M"
log_must eval "zfs projectspace -H -oname,used $QFS | grep $PRJID3 | grep 100\\.\*M"
log_must eval "zfs get -H -ovalue projectused@$PRJID3 $QFS | grep 100\\.\*M"


log_must user_run $PUSER mkfile 100m $PRJDIR/a/b/c/f2
sync_all_pools
zfs projectspace $QFS
log_must eval "zfs projectspace -H -oname,used $QFS | grep $PRJID1 | grep 400\\.\*M"
log_must eval "zfs get -H -ovalue projectused@$PRJID1 $QFS | grep 400\\.\*M"
log_must eval "zfs projectspace -H -oname,used $QFS | grep $PRJID2 | grep 300\\.\*M"
log_must eval "zfs get -H -ovalue projectused@$PRJID2 $QFS | grep 300\\.\*M"
log_must eval "zfs projectspace -H -oname,used $QFS | grep $PRJID3 | grep 200\\.\*M"
log_must eval "zfs get -H -ovalue projectused@$PRJID3 $QFS | grep 200\\.\*M"

log_note "Validate can't write at any level as quota exceeded at /a"
log_mustnot user_run $PUSER mkfile 100m $PRJDIR/a/f3
log_mustnot user_run $PUSER mkfile 100m $PRJDIR/a/b/f3
log_mustnot user_run $PUSER mkfile 100m $PRJDIR/a/b/c/f3

log_note "Remove Directory tree"
log_must rm -fR $PRJDIR/a
sync_all_pools
zfs projectspace $QFS
log_must zfs set projectquota@$PRJID1=none projectquota@$PRJID2=none projectquota@$PRJID3=none  $QFS

log_note "Setup Directory tree again"
log_must user_run $PUSER mkdir -p $PRJDIR/a/b/c
log_must user_run $PUSER mkfile 100m $PRJDIR/a/f1
log_must user_run $PUSER mkfile 100m $PRJDIR/a/b/f1
log_must user_run $PUSER mkfile 100m $PRJDIR/a/b/c/f1
sync_all_pools
zfs projectspace $QFS

log_note "Validate project assocation sequence /a/b/c, /a/b, /a"
log_must zfs set projectquota@$PRJID3=400m $QFS
log_must zfs project -p $PRJID3 -srY $PRJDIR/a/b/c
sync_all_pools
zfs projectspace $QFS
log_must eval "zfs projectspace -H -oname,used $QFS | grep $PRJID3 | grep 100\\.\*M"
log_must eval "zfs get -H -ovalue projectused@$PRJID3 $QFS | grep 100\\.\*M"

log_must zfs set projectquota@$PRJID2=400m $QFS
log_must zfs project -p $PRJID2 -srY $PRJDIR/a/b
sync_all_pools
zfs projectspace $QFS
log_must eval "zfs projectspace -H -oname,used $QFS | grep $PRJID2 | grep 200\\.\*M"
log_must eval "zfs get -H -ovalue projectused@$PRJID2 $QFS | grep 200\\.\*M"
log_must eval "zfs projectspace -H -oname,used $QFS | grep $PRJID3 | grep 100\\.\*M"
log_must eval "zfs get -H -ovalue projectused@$PRJID3 $QFS | grep 100\\.\*M"

log_must zfs set projectquota@$PRJID1=400m $QFS
log_must zfs project -p $PRJID1 -srY $PRJDIR/a
sync_all_pools
zfs projectspace $QFS
log_must eval "zfs projectspace -H -oname,used $QFS | grep $PRJID1 | grep 300\\.\*M"
log_must eval "zfs get -H -ovalue projectused@$PRJID1 $QFS | grep 300\\.\*M"
log_must eval "zfs projectspace -H -oname,used $QFS | grep $PRJID2 | grep 200\\.\*M"
log_must eval "zfs get -H -ovalue projectused@$PRJID2 $QFS | grep 200\\.\*M"
log_must eval "zfs projectspace -H -oname,used $QFS | grep $PRJID3 | grep 100\\.\*M"
log_must eval "zfs get -H -ovalue projectused@$PRJID3 $QFS | grep 100\\.\*M"

log_must user_run $PUSER mkfile 100m $PRJDIR/a/b/c/f2
sync_all_pools
zfs projectspace $QFS
log_must eval "zfs projectspace -H -oname,used $QFS | grep $PRJID1 | grep 400\\.\*M"
log_must eval "zfs get -H -ovalue projectused@$PRJID1 $QFS | grep 400\\.\*M"
log_must eval "zfs projectspace -H -oname,used $QFS | grep $PRJID2 | grep 300\\.\*M"
log_must eval "zfs get -H -ovalue projectused@$PRJID2 $QFS | grep 300\\.\*M"
log_must eval "zfs projectspace -H -oname,used $QFS | grep $PRJID3 | grep 200\\.\*M"
log_must eval "zfs get -H -ovalue projectused@$PRJID3 $QFS | grep 200\\.\*M"

log_note "Validate can't write at any level as quota exceeded at /a"
log_mustnot user_run $PUSER mkfile 100m $PRJDIR/a/f3
log_mustnot user_run $PUSER mkfile 100m $PRJDIR/a/b/f3
log_mustnot user_run $PUSER mkfile 100m $PRJDIR/a/b/c/f3

log_note "Remove Directory tree"
log_must rm -fR $PRJDIR/a
sync_all_pools
zfs projectspace $QFS
log_must zfs set projectquota@$PRJID1=none projectquota@$PRJID2=none projectquota@$PRJID3=none  $QFS

log_note "Setup Directory tree again"
log_must user_run $PUSER mkdir -p $PRJDIR/a/b/c
log_must user_run $PUSER mkfile 100m $PRJDIR/a/f1
log_must user_run $PUSER mkfile 100m $PRJDIR/a/b/f1
log_must user_run $PUSER mkfile 100m $PRJDIR/a/b/c/f1
sync_all_pools
zfs projectspace $QFS

log_note "Validate project assocation sequence /a, /a/b/c, /a/b"
log_must zfs set projectquota@$PRJID1=400m $QFS
log_must zfs project -p $PRJID1 -srY $PRJDIR/a
sync_all_pools
zfs projectspace $QFS
log_must eval "zfs projectspace -H -oname,used $QFS | grep $PRJID1 | grep 300\\.\*M"
log_must eval "zfs get -H -ovalue projectused@$PRJID1 $QFS | grep 300\\.\*M"

log_must zfs set projectquota@$PRJID3=400m $QFS
log_must zfs project -p $PRJID3 -srY $PRJDIR/a/b/c
sync_all_pools
zfs projectspace $QFS
log_must eval "zfs projectspace -H -oname,used $QFS | grep $PRJID1 | grep 300\\.\*M"
log_must eval "zfs get -H -ovalue projectused@$PRJID1 $QFS | grep 300\\.\*M"
log_must eval "zfs projectspace -H -oname,used $QFS | grep $PRJID3 | grep 100\\.\*M"
log_must eval "zfs get -H -ovalue projectused@$PRJID3 $QFS | grep 100\\.\*M"

log_must zfs set projectquota@$PRJID2=400m $QFS
log_must zfs project -p $PRJID2 -srY $PRJDIR/a/b
sync_all_pools
zfs projectspace $QFS
log_must eval "zfs projectspace -H -oname,used $QFS | grep $PRJID1 | grep 300\\.\*M"
log_must eval "zfs get -H -ovalue projectused@$PRJID1 $QFS | grep 300\\.\*M"
log_must eval "zfs projectspace -H -oname,used $QFS | grep $PRJID2 | grep 200\\.\*M"
log_must eval "zfs get -H -ovalue projectused@$PRJID2 $QFS | grep 200\\.\*M"
log_must eval "zfs projectspace -H -oname,used $QFS | grep $PRJID3 | grep 100\\.\*M"
log_must eval "zfs get -H -ovalue projectused@$PRJID3 $QFS | grep 100\\.\*M"

log_must user_run $PUSER mkfile 100m $PRJDIR/a/b/c/f2
sync_all_pools
zfs projectspace $QFS
log_must eval "zfs projectspace -H -oname,used $QFS | grep $PRJID1 | grep 400\\.\*M"
log_must eval "zfs get -H -ovalue projectused@$PRJID1 $QFS | grep 400\\.\*M"
log_must eval "zfs projectspace -H -oname,used $QFS | grep $PRJID2 | grep 300\\.\*M"
log_must eval "zfs get -H -ovalue projectused@$PRJID2 $QFS | grep 300\\.\*M"
log_must eval "zfs projectspace -H -oname,used $QFS | grep $PRJID3 | grep 200\\.\*M"
log_must eval "zfs get -H -ovalue projectused@$PRJID3 $QFS | grep 200\\.\*M"

log_note "Validate can't write at any level as quota exceeded at /a"
log_mustnot user_run $PUSER mkfile 100m $PRJDIR/a/f3
log_mustnot user_run $PUSER mkfile 100m $PRJDIR/a/b/f3
log_mustnot user_run $PUSER mkfile 100m $PRJDIR/a/b/c/f3

log_note "Validate file remove reduces usage in hierarchy"
log_must rm $PRJDIR/a/b/c/f2
sync_all_pools
zfs projectspace $QFS
log_must eval "zfs projectspace -H -oname,used $QFS | grep $PRJID1 | grep 300\\.\*M"
log_must eval "zfs get -H -ovalue projectused@$PRJID1 $QFS | grep 300\\.\*M"
log_must eval "zfs projectspace -H -oname,used $QFS | grep $PRJID2 | grep 200\\.\*M"
log_must eval "zfs get -H -ovalue projectused@$PRJID2 $QFS | grep 200\\.\*M"
log_must eval "zfs projectspace -H -oname,used $QFS | grep $PRJID3 | grep 100\\.\*M"
log_must eval "zfs get -H -ovalue projectused@$PRJID3 $QFS | grep 100\\.\*M"

log_note "Validate an existing project associated to directory, can't be associated to another directory"
log_must zfs project -p $PRJID1 -srY $PRJDIR/a
log_mustnot zfs project -p $PRJID1 -srY $PRJDIR/a/b
log_mustnot zfs project -p $PRJID1 -srY $PRJDIR/a/b/c
log_must zfs project -p $PRJID2 -srY $PRJDIR/a/b
log_mustnot zfs project -p $PRJID2 -srY $PRJDIR/a/
log_mustnot zfs project -p $PRJID2 -srY $PRJDIR/a//b/c
log_must zfs project -p $PRJID3 -srY $PRJDIR/a/b/c
log_mustnot zfs project -p $PRJID3 -srY $PRJDIR/a/
log_mustnot zfs project -p $PRJID3 -srY $PRJDIR/a/b

#Validate a new project can be associcated to directory path, if its
#assocaited to existing project without quota. Basically, quota on project
#removed, but project was associated to path in middle of heirarchy,
#so its assocation with project still exist due to accounting in hierarchy.
#Now new project quota and project assoication with path should be possible.
log_note "Validate a new project can be associcated to directory path"
log_must zfs set projectquota@$PRJID4=400m $QFS
log_mustnot zfs project -p $PRJID4 -srY $PRJDIR/a/b
log_must zfs set projectquota@$PRJID2=none $QFS
log_must zfs project -p $PRJID4 -srY $PRJDIR/a/b
sync_all_pools
zfs projectspace $QFS
log_must eval "zfs projectspace -H -oname,used $QFS | grep $PRJID1 | grep 300\\.\*M"
log_must eval "zfs get -H -ovalue projectused@$PRJID1 $QFS | grep 300\\.\*M"
log_must eval "zfs projectspace -H -oname,used $QFS | grep $PRJID4 | grep 200\\.\*M"
log_must eval "zfs get -H -ovalue projectused@$PRJID4 $QFS | grep 200\\.\*M"
log_must eval "zfs projectspace -H -oname,used $QFS | grep $PRJID3 | grep 100\\.\*M"
log_must eval "zfs get -H -ovalue projectused@$PRJID3 $QFS | grep 100\\.\*M"

log_note "Remove Directory tree"
log_must rm -fR $PRJDIR/a
sync_all_pools
zfs projectspace $QFS
log_must zfs set projectquota@$PRJID1=none projectquota@$PRJID4=none projectquota@$PRJID3=none  $QFS

log_pass "Validate Hierarchical project quota"
