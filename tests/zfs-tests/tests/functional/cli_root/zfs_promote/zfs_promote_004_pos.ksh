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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/zfs_promote/zfs_promote.cfg
. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	'zfs promote' can deal with multi-level clones.
#
# STRATEGY:
#	1. Create multiple snapshots and multi-level clones
#	2. Promote a clone filesystem
#	3. Verify the dataset dependency relationships are correct after promotion.
#

verify_runnable "both"

function cleanup
{
	if snapexists ${c1snap[1]}; then
		log_must zfs promote $clone
	fi

	typeset ds
	typeset data
	for ds in ${snap[*]}; do
		snapexists $ds && destroy_dataset $ds -rR
	done
	for data in ${file[*]}; do
		[[ -e $data ]] && rm -f $data
	done
}

log_assert "'zfs promote' can deal with multi-level clone."
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
clone=$TESTPOOL/$TESTCLONE
clone1=$TESTPOOL/$TESTCLONE1

# Define some arrays here to use loop to reduce code amount

# Array which stores the origin snapshots created in the origin filesystem
set -A snap "${fs}@$TESTSNAP" "${fs}@$TESTSNAP1" "${fs}@$TESTSNAP2" "${fs}@$TESTSNAP3"
# Array which stores the snapshots existing in the first clone
set -A csnap "${clone}@$TESTSNAP3" "${clone}@$TESTSNAP4" "${clone}@$TESTSNAP5"
# Array which stores the snapshots existing in the second clone after promote operation
set -A c1snap "${clone1}@$TESTSNAP3" "${clone1}@$TESTSNAP4" "${clone1}@$TESTSNAP5"
# The data will inject into the origin filesystem
set -A file "$TESTDIR/$TESTFILE0" "$TESTDIR/$TESTFILE1" "$TESTDIR/$TESTFILE2" \
		"$TESTDIR/$TESTFILE3"
cdir=/$TESTPOOL/$TESTCLONE
# The data will inject into the first clone
set -A cfile "${cdir}/$CLONEFILE" "${cdir}/$CLONEFILE1" "${cdir}/$CLONEFILE2"
c1snapdir=/$TESTPOOL/$TESTCLONE1/.zfs/snapshot
# The data which will exist in the snapshot of the second clone filesystem after promote
set -A c1snapfile "${c1snapdir}/$TESTSNAP3/$CLONEFILE" \
	"${c1snapdir}/$TESTSNAP4/$CLONEFILE1" \
	"${c1snapdir}/$TESTSNAP5/$CLONEFILE2"

# setup for promote testing
typeset -i i=0
while (( i < 4 )); do
	log_must mkfile $FILESIZE ${file[i]}
	(( i>0 )) && log_must rm -f ${file[((i-1))]}
	log_must zfs snapshot ${snap[i]}

	(( i = i + 1 ))
done
log_must zfs clone ${snap[2]} $clone

log_must rm -f /$clone/$TESTFILE2
i=0
while (( i < 3 )); do
	log_must mkfile $FILESIZE ${cfile[i]}
	(( i>0 )) && log_must rm -f ${cfile[(( i-1 ))]}
	log_must zfs snapshot ${csnap[i]}

	(( i = i + 1 ))
done

log_must zfs clone ${csnap[1]} $clone1
log_must mkfile $FILESIZE /$clone1/$CLONEFILE2
log_must rm -f /$clone1/$CLONEFILE1
log_must zfs snapshot ${c1snap[2]}

log_must zfs promote $clone1

# verify the 'promote' operation
for ds in ${snap[*]} ${csnap[2]} ${c1snap[*]}; do
	! snapexists $ds && \
		log_fail "The snapshot $ds disappear after zfs promote."
done
for data in ${c1snapfile[*]}; do
	[[ ! -e $data ]] && \
		log_fail "The data file $data loses after zfs promote."
done

origin_prop=$(get_prop origin $fs)
[[ "$origin_prop" != "-" ]] && \
	log_fail "The dependency is not correct for $fs after zfs promote."
origin_prop=$(get_prop origin $clone)
[[ "$origin_prop" != "${c1snap[1]}" ]] && \
	log_fail "The dependency is not correct for $clone after zfs promote."
origin_prop=$(get_prop origin $clone1)
[[ "$origin_prop" != "${snap[2]}" ]] && \
	log_fail "The dependency is not correct for $clone1 after zfs promote."

log_pass "'zfs promote' deal with multi-level clones as expected."

