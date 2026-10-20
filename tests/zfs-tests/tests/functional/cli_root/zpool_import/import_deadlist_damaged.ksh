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
# Copyright 2026 Oxide Computer Company
#

. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.kshlib

#
# DESCRIPTION:
#	Holding a snapshot whose deadlist object can't be read must fail
#	with an error, not crash the system.
#
# STRATEGY:
#	1. Create a pool with snapshots, until the MOS dnode block that holds
#	   a@s2's deadlist object holds nothing else than snapshots and
#	   snapshot deadlists that nothing reads at import (that is, not the
#	   latest snapshot of a file system), and a@s2 itself is in another
#	   dnode block.  MOS objects are allocated from per-CPU chunks, so
#	   this can take a few tries.  Check this on the exported pool.
#	2. Overwrite every copy of that dnode block.
#	3. Import the pool without spa_load_verify(), whose traversal holds
#	   the datasets in recently written dnode blocks and could read the
#	   damaged block itself.  Don't add the pool to the cache file, so
#	   that a system without the fix doesn't open it again at boot.
#	   After the import, the async thread's sweep for snapshots marked
#	   for deferred destroy holds every snapshot, including a@s2.
#	4. Hold a@s2 (zfs list), twice.  dsl_dataset_hold_obj() can't read
#	   the deadlist, so the hold must fail with an error.
#	5. Verify that a and a@s3 can still be held, and that a sync task
#	   still runs.
#

verify_runnable "global"

typeset -i MAXTRIES=10
# Dnodes per 16K MOS dnode block.
typeset -i DNODES_PER_BLOCK=32

function custom_cleanup
{
	set_spa_load_verify_metadata 1
	cleanup
}

#
# Read "zdb -dddd <pool>" output.  If a@s2's deadlist object is in a
# different dnode block than a@s2, and that block holds only snapshots and
# snapshot deadlists, print "<deadlist object> <block>".  Otherwise print
# "bad <reason>".
#
function check_layout # s2-dsobj
{
	awk -v s2=$1 -v dpb=$DNODES_PER_BLOCK '
	    $1 == "Object" {
		getline
		if ($1 ~ /^[0-9]+$/) {
			cur = $1
			objs[cur] = 1
		}
		next
	    }
	    /num_children =/ { nc[cur] = $3 }
	    /deadlist_obj =/ { dl[cur] = $3 }
	    /prev_snap_obj =/ { ps[cur] = $3 }
	    END {
		d = dl[s2]
		if (d == "") {
			print "bad no-deadlist"
			exit
		}
		b = int(d / dpb)
		if (b == int(s2 / dpb)) {
			print "bad same-block"
			exit
		}
		#
		# Heads and their latest snapshots (ds_prev) are held at
		# import, with their deadlists open.
		#
		for (x in nc) {
			if (nc[x] > 0)
				continue
			held[x] = 1
			held[dl[x]] = 1
			if (ps[x] != 0) {
				held[ps[x]] = 1
				held[dl[ps[x]]] = 1
			}
		}
		for (x in nc) {
			if (nc[x] > 0)
				snapdl[dl[x]] = 1
		}
		for (o in objs) {
			if (int(o / dpb) != b)
				continue
			if (o in held) {
				print "bad held object " o " in block " b
				exit
			}
			if ((o in nc) && nc[o] > 0)
				continue
			if (o in snapdl)
				continue
			print "bad object " o " in block " b
			exit
		}
		print d, b
	    }'
}

log_onexit custom_cleanup

log_assert "A snapshot hold that can't read the deadlist fails, not crashes"

typeset -i try=0
typeset layout=""
while ((try < MAXTRIES)); do
	((try = try + 1))
	log_must zpool create -f $TESTPOOL1 $VDEV0
	log_must zfs create $TESTPOOL1/a
	log_must zfs create $TESTPOOL1/b
	for ((i = 1; i <= 40; i++)); do
		log_must zfs snapshot $TESTPOOL1/b@f$i
	done
	log_must zfs snapshot $TESTPOOL1/a@s1
	for ((i = 1; i <= 40; i++)); do
		log_must zfs snapshot $TESTPOOL1/b@g$i
	done
	log_must zfs snapshot $TESTPOOL1/a@s2
	log_must zfs snapshot $TESTPOOL1/a@s3
	# Let the snapshots' dnode blocks age past spa_load_verify()'s range.
	for ((i = 1; i <= 8; i++)); do
		log_must zfs set user:pad=$i $TESTPOOL1
		log_must sync_pool $TESTPOOL1
	done

	s2=$(get_prop objsetid $TESTPOOL1/a@s2)
	#
	# Check the layout of the exported pool: the export's last txgs can
	# move blocks and allocate MOS objects (e.g. log space maps).  Name
	# it by guid, as zdb -e also finds destroyed pools of the same name.
	#
	guid=$(get_pool_prop guid $TESTPOOL1)
	log_must zpool export $TESTPOOL1
	layout=$(zdb -e -p $DEVICE_DIR -dddd $guid | check_layout $s2)
	log_note "try $try: a@s2 is MOS object $s2: $layout"
	[[ $layout != bad* ]] && break
	log_must zpool import -d $DEVICE_DIR $TESTPOOL1
	log_must zpool destroy $TESTPOOL1
done
[[ $layout == bad* ]] && \
    log_unsupported "no usable MOS layout in $MAXTRIES tries"

set -A l $layout
typeset -i dlobj=${l[0]}
typeset -i blk=${l[1]}

#
# Find the DVAs of MOS dnode block $blk (object 0, level 0) in the
# indirect block listing of the exported MOS.
#
off=$(printf "%x" $((blk * DNODES_PER_BLOCK * 512)))
dvas=$(zdb -e -p $DEVICE_DIR -dddddd $guid | awk -v off=$off '
    /Indirect blocks:/ { n++ }
    n == 1 && $2 == "L0" && $1 == off {
	for (i = 3; i <= NF; i++)
		if ($i ~ /^0:[0-9a-f]+:[0-9a-f]+$/)
			print $i
	exit
    }')
[[ -n $dvas ]] || log_fail "no DVAs for MOS dnode block $blk"
log_note "deadlist object $dlobj is in MOS dnode block $blk: $dvas"

for dva in $dvas; do
	set -A d $(echo $dva | tr ':' ' ')
	typeset -i offset=$((16#${d[1]}))
	typeset -i asize=$((16#${d[2]}))
	# DVA offsets start after the 4M of front labels and boot block.
	log_must dd if=/dev/urandom of=$VDEV0 bs=512 conv=notrunc \
	    seek=$(((4194304 + offset) / 512)) count=$((asize / 512))
done

log_must set_spa_load_verify_metadata 0
log_must zpool import -o cachefile=none -d $DEVICE_DIR $TESTPOOL1
log_must set_spa_load_verify_metadata 1

#
# The hold fails with ECKSUM.  Without the fix, the kernel instead
# dereferences a NULL dbuf in dsl_deadlist_close(), on the async thread or
# in the ioctl, which kills zfs before it prints anything.
#
for i in 1 2; do
	out=$(zfs list $TESTPOOL1/a@s2 2>&1)
	rv=$?
	log_note "zfs list $TESTPOOL1/a@s2: exit $rv: $out"
	((rv == 1)) || log_fail "holding $TESTPOOL1/a@s2 exited with $rv"
	[[ $out == *"cannot open '$TESTPOOL1/a@s2'"* ]] || \
	    log_fail "unexpected output holding $TESTPOOL1/a@s2"
done
log_must zfs list $TESTPOOL1/a@s3
log_must zfs list $TESTPOOL1/a
log_must zfs set user:prop=1 $TESTPOOL1/a
log_must sync_pool $TESTPOOL1

log_pass "A snapshot hold that can't read the deadlist fails, not crashes"
