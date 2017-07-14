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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/delegate/delegate_common.kshlib

#
# DESCRIPTION:
#	Verify option '-d' allow permission to the descendent datasets, and not
#	for this dataset itself.
#
# STRATEGY:
#	1. Create descendent datasets of $ROOT_TESTFS
#	2. Select user, group and everyone and set descendent permission
#	   separately.
#	3. Set descendent permissions to $ROOT_TESTFS or $ROOT_TESTVOL.
#	4. Verify those permissions are allowed to $ROOT_TESTFS's
#	   descendent dataset.
#	5. Verify the permissions are not allowed to $ROOT_TESTFS or
#	   $ROOT_TESTVOL.
#

verify_runnable "both"

log_assert "Verify option '-d' allow permission to the descendent datasets."
log_onexit restore_root_datasets

childfs=$ROOT_TESTFS/childfs

eval set -A dataset $DATASETS
typeset perms="snapshot,reservation,compression,checksum,userprop"

# Verify option '-d' only affect sub-datasets
log_must zfs create $childfs
for dtst in $DATASETS ; do
	log_must zfs allow -d $STAFF1 $perms $dtst
	log_must verify_noperm $dtst $perms $STAFF1
	if [[ $dtst == $ROOT_TESTFS ]]; then
		log_must verify_perm $childfs $perms $STAFF1
	fi
done

log_must restore_root_datasets

# Verify option '-d + -g' affect group in sub-datasets.
log_must zfs create $childfs
for dtst in $DATASETS ; do
	log_must zfs allow -d -g $STAFF_GROUP $perms $dtst
	log_must verify_noperm $dtst $perms $STAFF2
	if [[ $dtst == $ROOT_TESTFS ]]; then
		log_must verify_perm $childfs $perms $STAFF2
	fi
done

log_must restore_root_datasets

# Verify option '-d + -e' affect everyone in sub-datasets.
log_must zfs create $childfs
for dtst in $DATASETS ; do
	log_must zfs allow -d -e $perms $dtst
	log_must verify_noperm $dtst $perms $OTHER1 $OTHER2
	if [[ $dtst == $ROOT_TESTFS ]]; then
		log_must verify_perm $childfs $perms $OTHER1 $OTHER2
	fi
done

log_must restore_root_datasets

log_pass "Verify option '-d' allow permission to the descendent datasets pass."
