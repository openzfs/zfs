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
# Copyright (c) 2026, Patrick Domack. All rights reserved.
#

. $STF_SUITE/tests/functional/clonedup/clonedup.kshlib

#
# DESCRIPTION:
#	A run takes gang blocks like any other block.  In a group that
#	holds gang copies and a copy that is not a gang block, that
#	copy survives even when a gang copy is older or a snapshot
#	holds it, and the run redirects the gang copies onto it.  In a
#	group of gang copies only, the older one survives.  A redirect
#	of a gang copy counts all of its gang tree as saved.  No copy
#	moves onto a gang block that may hold fewer copies of the
#	data, as the gang header's DVA count does not tell.
#
# STRATEGY:
#	1. Each case gets a new pool.  Forcing every block of 100000
#	   bytes or more to gang makes each 128K block a gang block.
#	   The mixed cases use redundant_metadata=most.  There the
#	   gang header of file data has as many copies as the data,
#	   so a gang copy pairs with a copy that is not one.
#	2. Write a file with ganging forced, then copy it without.
#	   Run, and check that the copy kept its blocks, that the file
#	   shares them and has no gang block left, that saved is the
#	   allocated size of the file's gang blocks, and that both
#	   read back as written.
#	3. The same with a snapshot of the file taken before the
#	   copy.  The snapshot keeps its gang blocks, and the space
#	   counts as held by snapshots.
#	4. With the default redundant_metadata=all, write a file and
#	   a copy of it, both with ganging forced.  Run, and check
#	   that the copy shares the file's gang blocks.
#	5. In one dataset, write a gang file with copies=2, then a
#	   gang copy with copies=3.  Both have three DVAs, the gang
#	   headers' count.  Run, and check that the copy keeps its
#	   blocks and counts as a policy skip.
#	6. In one dataset, write a gang file with copies=1, then a
#	   copy that is not a gang block with copies=2.  Both have two
#	   DVAs.  Run, and check that the file moves onto the copy.
#	7. After each case the pool scrubs clean and zdb finds no
#	   leak.
#

verify_runnable "global"

function cleanup
{
	log_must restore_tunable METASLAB_FORCE_GANGING
	log_must restore_tunable METASLAB_FORCE_GANGING_PCT
	clonedup_cleanup
}

function gs_gang_on
{
	log_must set_tunable64 METASLAB_FORCE_GANGING 100000
	log_must set_tunable32 METASLAB_FORCE_GANGING_PCT 100
}

function gs_gang_off
{
	log_must set_tunable32 METASLAB_FORCE_GANGING_PCT 0
}

# A new pool with dataset ds, created with redundant_metadata red.
function gs_pool # ds red
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	clonedup_pool_create
	log_must zfs create -o redundant_metadata=$2 $TESTPOOL/$1
}

# Assert that every block of a file is a gang block, or none is.
function gs_check_gang # ds file all|none
{
	typeset out=$(mktemp)
	typeset -i n=0 gang=0
	typeset idx dva

	clonedup_file_blocks $1 $2 > $out
	while read idx dva; do
		[[ $dva == *G* ]] && ((gang += 1))
		((n += 1))
	done < $out
	rm -f $out
	log_note "$1 $2: $n blocks, $gang of them gang blocks"
	log_must [ $n -eq $CD_BLOCKS ]
	if [[ $3 == all ]]; then
		log_must [ $gang -eq $n ]
	else
		log_must [ $gang -eq 0 ]
	fi
}

# Every DVA of each of a file's blocks, one line per block.
function gs_dvas # ds file
{
	typeset obj=$(ls -i /$1/$2 | awk '{ print $1 }')
	typeset re='^[0-9]+:[0-9a-f]+:[0-9a-f]+G?$'

	zdb -dddddd $1 $obj | awk -v re="$re" '
	    / L0 / {
		for (i = 1; i <= NF && $i != "L0"; i++)
			;
		line = ""
		while (++i <= NF && $i ~ re)
			line = line " " $i
		print substr(line, 2)
	    }'
}

# The allocated size of a file's blocks, over every DVA of each.
function gs_asize # ds file
{
	typeset -i sum=0 v
	typeset d

	for d in $(gs_dvas $1 $2); do
		d=${d##*:}
		v=16#${d%G}
		((sum += v))
	done
	echo $sum
}

# Assert that every block of a file has n DVAs.
function gs_check_ndvas # ds file n
{
	typeset out=$(mktemp)
	typeset -i n=0 bad=0 want=$3 got
	typeset -a d

	gs_dvas $1 $2 > $out
	while read -A d; do
		got=${#d[@]}
		((got == want)) || ((bad += 1))
		((n += 1))
	done < $out
	rm -f $out
	log_note "$1 $2: $n blocks, $bad of them without $3 DVAs"
	log_must [ $n -eq $CD_BLOCKS -a $bad -eq 0 ]
}

# Both files read back as written.
function gs_check_data # ds file1 file2 digest
{
	log_must [ "$(xxh128digest /$TESTPOOL/$1/$2)" = "$4" ]
	log_must [ "$(xxh128digest /$TESTPOOL/$1/$3)" = "$4" ]
}

function gs_verify
{
	log_must zpool scrub -w $TESTPOOL
	log_must eval "zpool status $TESTPOOL |" \
	    "grep -q 'No known data errors'"
	clonedup_leakcheck
}

log_assert "a copy that is not a gang block survives over gang copies"
log_onexit cleanup
log_must save_tunable METASLAB_FORCE_GANGING
log_must save_tunable METASLAB_FORCE_GANGING_PCT

typeset all="$(clonedup_all_blocks)"
typeset hash keep
typeset -i asz

# A gang copy older than a copy that is not one.
gs_pool most most
gs_gang_on
clonedup_write /$TESTPOOL/most/g
clonedup_sync
gs_gang_off
clonedup_dup /$TESTPOOL/most/g /$TESTPOOL/most/n
clonedup_sync
gs_check_gang $TESTPOOL/most g all
gs_check_gang $TESTPOOL/most n none
hash=$(xxh128digest /$TESTPOOL/most/g)
keep=$(clonedup_file_blocks $TESTPOOL/most n)
asz=$(gs_asize $TESTPOOL/most g)
log_must [ $asz -gt $((CD_BLOCKS * CD_BS)) ]
clonedup_run
log_must [ "$(clonedup_file_blocks $TESTPOOL/most n)" = "$keep" ]
clonedup_check_shared $TESTPOOL/most g $TESTPOOL/most n "$all"
gs_check_gang $TESTPOOL/most g none
clonedup_jstat_is clonedup_applied $CD_BLOCKS
clonedup_jstat_is clonedup_saved $asz
clonedup_jstat_is clonedup_saved_snapheld 0
clonedup_jstat_is errors 0
gs_verify
gs_check_data most g n $hash

# The gang copy also held by a snapshot.
gs_pool most most
gs_gang_on
clonedup_write /$TESTPOOL/most/g
clonedup_sync
gs_gang_off
log_must zfs snapshot $TESTPOOL/most@s
clonedup_dup /$TESTPOOL/most/g /$TESTPOOL/most/n
clonedup_sync
gs_check_gang $TESTPOOL/most@s g all
gs_check_gang $TESTPOOL/most n none
hash=$(xxh128digest /$TESTPOOL/most/g)
keep=$(clonedup_file_blocks $TESTPOOL/most n)
asz=$(gs_asize $TESTPOOL/most g)
clonedup_run
log_must [ "$(clonedup_file_blocks $TESTPOOL/most n)" = "$keep" ]
clonedup_check_shared $TESTPOOL/most g $TESTPOOL/most n "$all"
clonedup_check_shared $TESTPOOL/most@s g $TESTPOOL/most n ""
gs_check_gang $TESTPOOL/most g none
gs_check_gang $TESTPOOL/most@s g all
clonedup_jstat_is clonedup_applied $CD_BLOCKS
clonedup_jstat_is clonedup_saved $asz
clonedup_jstat_is clonedup_saved_snapheld $asz
clonedup_jstat_is errors 0
gs_verify
gs_check_data most g n $hash

# Gang copies only.
gs_pool all all
gs_gang_on
clonedup_write /$TESTPOOL/all/a
clonedup_sync
clonedup_dup /$TESTPOOL/all/a /$TESTPOOL/all/b
clonedup_sync
gs_gang_off
gs_check_gang $TESTPOOL/all a all
gs_check_gang $TESTPOOL/all b all
hash=$(xxh128digest /$TESTPOOL/all/a)
keep=$(clonedup_file_blocks $TESTPOOL/all a)
asz=$(gs_asize $TESTPOOL/all b)
clonedup_run
log_must [ "$(clonedup_file_blocks $TESTPOOL/all a)" = "$keep" ]
clonedup_check_shared $TESTPOOL/all a $TESTPOOL/all b "$all"
gs_check_gang $TESTPOOL/all b all
clonedup_jstat_is clonedup_applied $CD_BLOCKS
clonedup_jstat_is clonedup_saved $asz
clonedup_jstat_is clonedup_saved_snapheld 0
clonedup_jstat_is errors 0
gs_verify
gs_check_data all a b $hash

# Gang copies whose data has two and three copies.  The one with
# three would lose a copy on the older one.
gs_pool all all
log_must zfs set copies=2 $TESTPOOL/all
gs_gang_on
clonedup_write /$TESTPOOL/all/c2
clonedup_sync
log_must zfs set copies=3 $TESTPOOL/all
clonedup_dup /$TESTPOOL/all/c2 /$TESTPOOL/all/c3
clonedup_sync
gs_gang_off
gs_check_gang $TESTPOOL/all c2 all
gs_check_gang $TESTPOOL/all c3 all
gs_check_ndvas $TESTPOOL/all c2 3
gs_check_ndvas $TESTPOOL/all c3 3
hash=$(xxh128digest /$TESTPOOL/all/c2)
keep=$(clonedup_file_blocks $TESTPOOL/all c3)
clonedup_run
log_must [ "$(clonedup_file_blocks $TESTPOOL/all c3)" = "$keep" ]
clonedup_check_shared $TESTPOOL/all c2 $TESTPOOL/all c3 ""
clonedup_jstat_is clonedup_applied 0
clonedup_jstat_is clonedup_skipped_policy $CD_BLOCKS
clonedup_jstat_is errors 0
gs_verify
gs_check_data all c2 c3 $hash

# A gang copy with one copy of its data, older than a copy that is
# not a gang block and has two.
gs_pool all all
gs_gang_on
clonedup_write /$TESTPOOL/all/g1
clonedup_sync
gs_gang_off
log_must zfs set copies=2 $TESTPOOL/all
clonedup_dup /$TESTPOOL/all/g1 /$TESTPOOL/all/n2
clonedup_sync
gs_check_gang $TESTPOOL/all g1 all
gs_check_gang $TESTPOOL/all n2 none
gs_check_ndvas $TESTPOOL/all g1 2
gs_check_ndvas $TESTPOOL/all n2 2
hash=$(xxh128digest /$TESTPOOL/all/g1)
keep=$(clonedup_file_blocks $TESTPOOL/all n2)
clonedup_run
log_must [ "$(clonedup_file_blocks $TESTPOOL/all n2)" = "$keep" ]
clonedup_check_shared $TESTPOOL/all g1 $TESTPOOL/all n2 "$all"
gs_check_gang $TESTPOOL/all g1 none
gs_check_ndvas $TESTPOOL/all g1 2
clonedup_jstat_is clonedup_applied $CD_BLOCKS
clonedup_jstat_is errors 0
gs_verify
gs_check_data all g1 n2 $hash

log_pass "a copy that is not a gang block survives over gang copies"
