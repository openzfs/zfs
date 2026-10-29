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
#	When a group has copies on the special class and on the normal
#	class, the copy on the special class survives, whichever copy
#	is older and even when a snapshot holds the normal one.  When
#	the group has a special copy, an older block the match walk
#	finds is its source only if that block is on the special class
#	too.  With zfs_bclone_strict_properties set, the datasets'
#	checksum, compression, copies and dedup must still match.
#
# STRATEGY:
#	1. Each case gets a new pool with a special vdev, and datasets
#	   plain and tier with special_small_blocks 0 and 128K, so
#	   tier's blocks are on the special vdev and plain's are not.
#	2. With the special copy older, and then with the normal copy
#	   older, a run leaves both sharing one block on the special
#	   vdev.
#	3. With a snapshot holding the normal copy, the run moves the
#	   head's copy onto the special block, and the snapshot keeps
#	   its own.
#	4. A normal block older than the run's window does not replace
#	   a new special copy.  Of a normal and a special older block,
#	   the special one becomes the source of a new copy.
#	5. After plain's compression changes, the run clones nothing
#	   and counts every candidate as a policy skip.  With
#	   zfs_bclone_strict_properties clear, a full run clones them.
#

verify_runnable "global"

function cleanup
{
	log_must restore_tunable BCLONE_STRICT_PROPERTIES
	clonedup_cleanup
}

log_assert "the copy on the special class survives"
log_onexit cleanup
log_must save_tunable BCLONE_STRICT_PROPERTIES
log_must set_tunable32 BCLONE_STRICT_PROPERTIES 1

typeset -a disks=($(clonedup_disks))
(( ${#disks[@]} >= 2 )) || log_unsupported "needs two disks"
typeset all="$(clonedup_all_blocks)"
typeset special=""

#
# The top-level vdev id of the pool's special vdev.  zpool status -g
# lists it by guid under "special", and zdb -C pairs guids with ids.
#
function special_vdev
{
	typeset guid=$(zpool status -g $TESTPOOL | awk '
	    $1 == "special" { s = 1; next }
	    s && $1 ~ /^[0-9]+$/ { print $1; exit }')

	[[ -n $guid ]] || return 0
	zdb -C $TESTPOOL | awk -v g=$guid '
	    $1 == "id:" { id = $2 }
	    $1 == "guid:" && $2 == g { print id; exit }'
}

# A new pool with a special vdev, and the special vdev's id.
function special_pool
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	log_must zpool create -f -o feature@clonedup=enabled \
	    -O recordsize=128k -O compression=off $TESTPOOL \
	    ${disks[0]} special ${disks[1]}
	log_must zfs create -o special_small_blocks=0 $TESTPOOL/plain
	log_must zfs create -o special_small_blocks=128k \
	    $TESTPOOL/tier
	special=$(special_vdev)
	log_note "special vdev id: '$special'"
	[[ $special == +([0-9]) ]] ||
	    log_fail "no special vdev id for $TESTPOOL"
}

#
# Assert that every block of a file has DVA[0] on the special vdev,
# or with "normal" that none has, and log the vdev of each block.
#
function check_class # ds file special|normal
{
	typeset out=$(mktemp)
	typeset -i n=0 bad=0
	typeset idx dva v

	clonedup_file_blocks $1 $2 > $out
	while read idx dva; do
		v=${dva%%:*}
		log_note "$1 $2 block $idx: DVA $dva, vdev $v"
		if [[ $3 == special ]]; then
			[[ $v == $special ]] || ((bad += 1))
		else
			[[ $v != $special ]] || ((bad += 1))
		fi
		((n += 1))
	done < $out
	rm -f $out
	log_note "$1 $2: $n blocks, $bad not $3, special is $special"
	log_must [ $n -eq $CD_BLOCKS -a $bad -eq 0 ]
}

# The special copy older.
special_pool
clonedup_write /$TESTPOOL/tier/a
clonedup_sync
clonedup_dup /$TESTPOOL/tier/a /$TESTPOOL/plain/b
clonedup_sync
check_class $TESTPOOL/tier a special
check_class $TESTPOOL/plain b normal
clonedup_run
clonedup_check_shared $TESTPOOL/tier a $TESTPOOL/plain b "$all"
check_class $TESTPOOL/plain b special
clonedup_jstat_is errors 0
clonedup_leakcheck

# The normal copy older.
special_pool
clonedup_write /$TESTPOOL/plain/c
clonedup_sync
clonedup_dup /$TESTPOOL/plain/c /$TESTPOOL/tier/d
clonedup_sync
check_class $TESTPOOL/plain c normal
check_class $TESTPOOL/tier d special
clonedup_run
clonedup_check_shared $TESTPOOL/plain c $TESTPOOL/tier d "$all"
check_class $TESTPOOL/plain c special
clonedup_jstat_is errors 0
clonedup_leakcheck

# The normal copy held by a snapshot, the special copy live.
special_pool
clonedup_write /$TESTPOOL/plain/e
clonedup_sync
log_must zfs snapshot $TESTPOOL/plain@s
# written since plain@s, so the run may still rewrite plain/e
clonedup_write /$TESTPOOL/plain/n 1
clonedup_dup /$TESTPOOL/plain/e /$TESTPOOL/tier/f
clonedup_sync
check_class $TESTPOOL/plain@s e normal
check_class $TESTPOOL/tier f special
clonedup_run
clonedup_check_shared $TESTPOOL/plain e $TESTPOOL/tier f "$all"
clonedup_check_shared $TESTPOOL/plain@s e $TESTPOOL/tier f ""
check_class $TESTPOOL/plain e special
check_class $TESTPOOL/plain@s e normal
clonedup_jstat_is errors 0
clonedup_leakcheck

#
# Older blocks.  The first run sees on alone, so the second run's
# window opens after on, and its match walk finds on as the older
# block for os.
#
special_pool
clonedup_write /$TESTPOOL/plain/on
clonedup_sync
clonedup_run
clonedup_dup /$TESTPOOL/plain/on /$TESTPOOL/tier/os
clonedup_sync
check_class $TESTPOOL/plain on normal
check_class $TESTPOOL/tier os special
clonedup_run
clonedup_check_shared $TESTPOOL/plain on $TESTPOOL/tier os ""
check_class $TESTPOOL/tier os special
# Now both are older than the run's window, and only os is special.
clonedup_dup /$TESTPOOL/plain/on /$TESTPOOL/plain/nw
clonedup_sync
check_class $TESTPOOL/plain nw normal
clonedup_run
clonedup_check_shared $TESTPOOL/tier os $TESTPOOL/plain nw "$all"
clonedup_check_shared $TESTPOOL/plain on $TESTPOOL/plain nw ""
check_class $TESTPOOL/plain nw special
check_class $TESTPOOL/plain on normal
clonedup_jstat_is errors 0
clonedup_leakcheck

# A compression mismatch still refuses.
special_pool
clonedup_write /$TESTPOOL/plain/g
clonedup_sync
clonedup_dup /$TESTPOOL/plain/g /$TESTPOOL/tier/h
clonedup_sync
log_must zfs set compression=lz4 $TESTPOOL/plain
clonedup_run
clonedup_check_shared $TESTPOOL/plain g $TESTPOOL/tier h ""
clonedup_jstat_is clonedup_skipped_policy $CD_BLOCKS
check_class $TESTPOOL/plain g normal
check_class $TESTPOOL/tier h special

log_must set_tunable32 BCLONE_STRICT_PROPERTIES 0
clonedup_run -f
clonedup_check_shared $TESTPOOL/plain g $TESTPOOL/tier h "$all"
check_class $TESTPOOL/plain g special
log_must set_tunable32 BCLONE_STRICT_PROPERTIES 1
clonedup_leakcheck

log_pass "the copy on the special class survives"
