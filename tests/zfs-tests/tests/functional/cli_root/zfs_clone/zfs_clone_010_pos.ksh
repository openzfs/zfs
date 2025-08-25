#!/bin/ksh
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.

# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Verify 'zfs list -t all -o name,origin,clones' prints the correct
#	clone information
#
# STRATEGY:
#	1. Create datasets
#	2. Create recursive snapshots and their clones
#	3. Verify zfs clones property displays right information for different
#	   cases
#

verify_runnable "both"

function local_cleanup
{
	typeset -i i=1
	for ds in $datasets; do
                datasetexists $ds/$TESTCLONE.$i && \
		    destroy_dataset $ds/$TESTCLONE.$i -rf
                datasetexists $ds && destroy_dataset $ds -Rf
		((i=i+1))
	done
}

# Set up filesystem with clones
function setup_ds
{
	typeset -i i=1
	# create nested datasets
	log_must zfs create -p $TESTPOOL/$TESTFS1/$TESTFS2/$TESTFS3

	# verify dataset creation
	for ds in $datasets; do
		datasetexists $ds || log_fail "Create $ds dataset fail."
	done

	# create recursive nested snapshot
	log_must zfs snapshot -r $TESTPOOL/$TESTFS1@snap
	for ds in $datasets; do
		datasetexists $ds@snap || \
		    log_fail "Create $ds@snap snapshot fail."
	done
	for ds in $datasets; do
		for fs in $datasets; do
			log_must zfs clone $ds@snap $fs/$TESTCLONE.$i
		done
		((i=i+1))
	done
}

# Verify clone list
function verify_clones
{
	typeset -i no_clones=$1
	typeset unexpected=$2
	typeset clone_snap=$3
	typeset -i i=1
	for ds in $datasets; do
		if [[ -n $clone_snap ]]; then
			clone_snap=/$TESTCLONE.$i
		fi
		snapshot=$(echo "$names" | grep $ds$clone_snap@snap)
		actual_clone=$(zfs list -t all -o clones $snapshot | tail -1)
		save=$IFS
		IFS=','
		typeset -a clones=()
		for token in $actual_clone; do
			clones=( "${clones[@]}" "$token" )
		done
		IFS=$save
		[[ ${#clones[*]} -ne $no_clones ]] && \
		    log_fail "$snapshot has unexpected number of clones" \
		        " ${#clones[*]}"
		expected_clone=""
		unexpected_clone=""
		if [[ $unexpected -eq 1 ]]; then
			for fs in $datasets; do
				if [[ $fs == $ds ]]; then
					if [[ -z $clone_snap ]]; then
						unexpected_clone=$fs/$TESTCLONE.$i
						(for match in ${clones[@]};do
						[[ $match != $unexpected_clone ]] && \
						    exit 0; done) || log_fail \
					            "Unexpected clones of the snapshot"
					else
						expected_clone=$fs
						unexpected_clone=$fs/$TESTCLONE.$i
						(for match in ${clones[@]};do
						[[ $match == $expected_clone ]] && \
						    [[ $match != $unexpected_clone ]] \
						    && exit 0; done) || log_fail \
						    "Unexpected clones of the snapshot"
					fi
				else
					expected_clone=$fs/$TESTCLONE.$i
					(for match in ${clones[@]};do
					[[ $match == $expected_clone ]] && \
					    exit 0; done) || log_fail \
					    "Unexpected clones of the snapshot"
				fi
			done
		else
			for fs in $datasets; do
				expected_clone=$fs/$TESTCLONE.$i
				(for match in ${clones[@]};do
				[[ $match == $expected_clone ]] && exit 0; \
				    done) || log_fail "Unexpected clones" \
				    " of the snapshot"
			done
		fi
		((i=i+1))
	done
}


log_onexit local_cleanup
datasets="$TESTPOOL/$TESTFS1 $TESTPOOL/$TESTFS1/$TESTFS2
    $TESTPOOL/$TESTFS1/$TESTFS2/$TESTFS3"

typeset -a d_clones
typeset -a deferred_snaps
typeset -i i
log_must setup_ds

log_note "Verify zfs clone property for multiple clones"
names=$(zfs list -rt all -o name $TESTPOOL)
log_must verify_clones 3 0

log_note "verify clone property for clone deletion"
i=1
for ds in $datasets; do
	log_must zfs destroy $ds/$TESTCLONE.$i
	((i=i+1))
done
names=$(zfs list -rt all -o name $TESTPOOL)
log_must verify_clones 2 1

log_must local_cleanup
log_must setup_ds

log_note "verify zfs deferred destroy on clones property"
names=$(zfs list -rt all -o name $TESTPOOL)
for ds in $datasets; do
	log_must zfs destroy -d $ds@snap
	deferred_snaps=( "${deferred_snaps[@]}" "$ds@snap" )
done
log_must verify_clones 3 0

log_note "verify zfs deferred destroy by destroying clones on clones property"
d_clones=()
i=1
for ds in $datasets; do
	for fs in $datasets; do
		log_must zfs destroy $fs/$TESTCLONE.$i
		d_clones=( "${d_clones[@]}" "$fs/$TESTCLONE.$i" )
	done
	((i=i+1))
done
names=$(zfs list -rtall -o name $TESTPOOL)
for snap in ${deferred_snaps[@]}; do
	status=$(echo "$names" | grep $snap)
	[[ -z $status ]] || \
	    log_fail "$snap exist after deferred destroy"
done
for dclone in ${d_clones[@]}; do
	log_note "D CLONE = $dclone"
	status=$(echo "$names" | grep $dclone)
	[[ -z $status ]] || \
	    log_fail "$dclone exist after deferred destroy"
done

log_must local_cleanup
log_must setup_ds
log_note "verify clone property for zfs promote"
i=1
for ds in $datasets; do
	log_must zfs promote $ds/$TESTCLONE.$i
	((i=i+1))
done
names=$(zfs list -rt all -o name,clones $TESTPOOL)
log_must verify_clones 3 1 $TESTCLONE
for ds in $datasets; do
	log_must zfs promote $ds
done
log_must local_cleanup

log_note "verify clone list truncated correctly"
fs=$TESTPOOL/$TESTFS1
xs=""; for i in {1..200}; do xs+="x"; done
maxproplen=8192
log_must zfs create $fs
log_must zfs snapshot $fs@snap
for (( i = 1; i <= (maxproplen / 200 + 1); i++ )); do
	log_must zfs clone ${fs}@snap ${fs}/${TESTCLONE}${xs}.${i}
done
clone_list=$(zfs list -o clones $fs@snap)
char_count=$(echo "$clone_list" | tail -1 | wc -c)
[[ $char_count -eq $maxproplen ]] || \
    log_fail "Clone list not truncated correctly. Unexpected character count" \
        "$char_count"

log_pass "'zfs list -o name,origin,clones prints the correct clone information."
