#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
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
# CDDL HEADER END
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	A config-only parity-epoch table may describe live blocks.  Preserve
#	it for read-only recovery.  Refuse a writable import while the pool
#	may hold blocks of an epoch it adds, and drop the table on a writable
#	import otherwise.  The refusal and recovery reads must leave the
#	labels and MOS config unchanged.  A table in the vdev ZAP only remains
#	authoritative and is propagated to the config on a writable import.
#	The first txg a writable import writes rewrites labels that may
#	describe another layout than the one it writes with: those of a
#	dropped table, and those a rewind leaves from the txgs it discards.
#
# STRATEGY:
#	1. Create config-only tables with future and reached parity epochs.
#	2. For the reached epoch, refuse a writable import and compare all
#	   vdev bytes before/after.
#	3. Read the known block through a read-only import, and compare all
#	   vdev bytes again.  Verify that labels and MOS config keep the table.
#	4. For the future epoch, require a writable import that crashes after
#	   its first txg to have dropped the table from the labels, and a
#	   writable import then to drop it from the MOS config too, and the
#	   known block to read back afterwards.
#	5. Store a ZAP-only table and verify its normal import and propagation.
#	6. Store a table with zhack, rewind a writable import to the txg
#	   before it or to a checkpoint taken before it, both of which the
#	   import writes past, and crash after its first txg: the labels must
#	   have lost the table, and the pool must import and read back the
#	   known block.  Protect the txg used by -T with a checkpoint too.
#	7. At the boundary of the txgs whose blocks the ZIL may hold, the last
#	   synced txg plus three: refuse a writable import for an epoch that
#	   starts there with another parity, or only another width, and drop
#	   a table whose epoch starts one txg later or has the vdev's layout.
#	   With zpool import -F, take the boundary from the txg rewound to.
#

verify_runnable "global"

typeset -r BASEDIR=$TEST_BASE_DIR/raidz_pe_label.$$
typeset -r POOL=${TESTPOOL}_label
typeset -r FAR=1000000
typeset -r MSG_KEEP="preserving the config table for read-only recovery"
typeset -r MSG_ZAP="no parity epoch table in the config, using"
typeset -i WIDTH=5
typeset -i n
typeset dir out start birth obj first how to saved

function cleanup
{
	rm -rf $BASEDIR
}

# count_values <dir> <label|config>: set n to the values of the table there.
function count_values
{
	typeset d=$1 where=$2

	if [[ $where == label ]]; then
		log_must eval "zdb -l $d/v0 > $d/where.out"
	else
		log_must eval "zdb -e -p $d -C $POOL > $d/where.out"
	fi
	n=$(grep -c 'raidz_parity_epochs\[' $d/where.out)
}

# config_case <future|reached>: preserve the table and the known block.
function config_case
{
	typeset name=$1
	dir=$BASEDIR/$name
	log_must mkdir -p $dir
	log_must raidz_epoch_test create $dir $POOL $WIDTH 1
	if [[ $name == reached ]]; then
		out=$(raidz_epoch_test label $dir $POOL now) ||
		    log_fail "label failed: $out"
		log_note "$out"
		start=$(print -r -- "$out" |
		    sed -n 's/.* start=\([0-9]*\).*/\1/p')
		birth=$(print -r -- "$out" |
		    sed -n 's/.* birth=\([0-9]*\).*/\1/p')
		[[ $start == +([0-9]) && $birth == +([0-9]) ]] ||
		    log_fail "no start or birth txg in: $out"
		(( birth >= start )) || log_fail "epoch was not reached: $out"
	else
		out=$(raidz_epoch_test write $dir $POOL 8192) ||
		    log_fail "write failed: $out"
		log_must raidz_epoch_test label $dir $POOL
	fi
	obj=$(print -r -- "$out" |
	    sed -n 's/.* object=\([0-9]*\).*/\1/p')
	[[ $obj == +([0-9]) ]] || log_fail "no object in: $out"

	count_values $dir label
	(( n >= 6 )) || log_fail "the labels do not carry the table"
	log_must eval "cksum $dir/v* > $dir/before.cksum"
	if [[ $name == reached ]]; then
		log_mustnot raidz_epoch_test write $dir $POOL 8192
		log_must eval "cksum $dir/v* > $dir/after.cksum"
		log_must cmp $dir/before.cksum $dir/after.cksum
	fi
	out=$(raidz_epoch_test read $dir $POOL $obj 8192 readonly) ||
	    log_fail "read-only import failed: $out"
	log_note "$out"
	[[ $out == *"rc=0 match=1"* ]] || log_fail "read mismatch: $out"
	log_must eval "zdb -e -p $dir -G -m $POOL > $dir/zdb.out 2>&1"
	log_must grep -q "$MSG_KEEP" $dir/zdb.out
	count_values $dir label
	(( n >= 6 )) || log_fail "the labels lost the recovery table"
	count_values $dir config
	(( n == 6 )) || log_fail "the MOS config lost the recovery table"
	log_must eval "cksum $dir/v* > $dir/after.cksum"
	log_must cmp $dir/before.cksum $dir/after.cksum
	[[ $name == reached ]] && return

	# No block can be born in the future epoch yet: a writable import
	# drops the table and writes the config without it, from its first
	# txg on.
	out=$(raidz_epoch_test crash $dir $POOL) ||
	    log_fail "a writable import refused a table nothing uses: $out"
	log_note "$out"
	[[ $out == *" first="[1-9]* ]] || log_fail "no crash point: $out"
	count_values $dir label
	(( n == 0 )) || log_fail "the first txg kept the dropped table: $out"
	out=$(raidz_epoch_test write $dir $POOL 8192) ||
	    log_fail "a writable import refused a table nothing uses: $out"
	log_note "$out"
	count_values $dir label
	(( n == 0 )) || log_fail "the labels still carry the dropped table"
	count_values $dir config
	(( n == 0 )) || log_fail "the MOS config still carries the table"
	out=$(raidz_epoch_test read $dir $POOL $obj 8192) ||
	    log_fail "import after the drop failed: $out"
	log_note "$out"
	[[ $out == *"rc=0 match=1"* ]] || log_fail "read mismatch: $out"
}

# boundary_case <name> <txgs> <width> <parity> <last> <want>: give the labels
# and the MOS config a table whose added epoch starts <txgs> after the txg T
# of the task that writes it, with T + <last> the last synced txg, and want
# a writable import to refuse it, to drop it, or, as zpool import -F, to
# drop it at the txg it rewinds to once the last one is refused.
function boundary_case
{
	typeset name=$1 after=$2 width=$3 parity=$4 last=$5 want=$6
	typeset txg ub

	dir=$BASEDIR/boundary_$name
	log_must mkdir -p $dir
	log_must raidz_epoch_test create $dir $POOL $WIDTH 1
	out=$(raidz_epoch_test write $dir $POOL 8192) ||
	    log_fail "write failed: $out"
	obj=$(print -r -- "$out" | sed -n 's/.* object=\([0-9]*\).*/\1/p')
	[[ $obj == +([0-9]) ]] || log_fail "no object in: $out"
	out=$(raidz_epoch_test label $dir $POOL at $after $width $parity \
	    $last) || log_fail "label failed: $out"
	log_note "$name: $out"
	txg=$(print -r -- "$out" | sed -n 's/.* txg=\([0-9]*\).*/\1/p')
	ub=$(zdb -lu $dir/v0 |
	    sed -n 's/^[[:space:]]*txg = \([0-9]*\)$/\1/p' | sort -n | tail -1)
	(( ub == txg + last )) ||
	    log_fail "$name: the last synced txg is $ub, not $txg + $last"

	out=$(raidz_epoch_test write $dir $POOL 8192)
	log_note "$name: $out"
	if [[ $want != dropped ]]; then
		[[ $out == *"RESULT write rc=30"* ]] ||
		    log_fail "$name: the epoch was not refused: $out"
		[[ $want == refused ]] && return
		out=$(raidz_epoch_test recover $dir $POOL)
		log_note "$name: $out"
		[[ $out == *"RESULT recover rc=0"* ]] ||
		    log_fail "$name: -F did not drop the epoch: $out"
	else
		[[ $out == *"RESULT write rc=0 "* ]] ||
		    log_fail "$name: the epoch was not dropped: $out"
	fi
	count_values $dir label
	(( n == 0 )) || log_fail "$name: the labels kept the dropped table"
	out=$(raidz_epoch_test read $dir $POOL $obj 8192) ||
	    log_fail "$name: import after the drop failed: $out"
	[[ $out == *"rc=0 match=1"* ]] || log_fail "$name: read mismatch: $out"
}

log_onexit cleanup
log_assert "config-only epochs preserve data through read-only recovery"
config_case future
config_case reached

# A table in the vdev ZAP only: used, and written back to the config.
dir=$BASEDIR/zap
log_must mkdir -p $dir
log_must raidz_epoch_test create $dir $POOL $WIDTH 1
log_must zhack -d $dir raidz_epochs -f $POOL 0 0:$WIDTH:1 $FAR:$WIDTH:2
count_values $dir label
(( n == 0 )) || log_fail "zhack -f wrote the table to the labels"
log_must eval "zdb -e -p $dir -G -m $POOL > $dir/zdb.out 2>&1"
log_must grep -q "$MSG_ZAP the 2 entries of the vdev ZAP's" $dir/zdb.out
log_must grep -q "raidz_parity_epochs 2: \[txg>=0 w=$WIDTH p=1\]" \
    $dir/zdb.out
out=$(raidz_epoch_test write $dir $POOL 8192) || log_fail "write failed: $out"
log_note "$out"
count_values $dir label
(( n >= 6 )) || log_fail "the labels written back lack the table"
count_values $dir config
(( n == 6 )) || log_fail "the MOS config written back lacks the table"

# Rewinding a writable import to before a table zhack stored, to the txg
# before it or to a checkpoint, finds the table in labels newer than the
# MOS config it loads.  The import writes from a txg above every uberblock
# on disk, past the start of the epoch the table adds, so its first txg
# must rewrite the labels: a crash after it would leave labels by which its
# blocks are read with the parity of the epoch.
for how in txg checkpoint; do
	dir=$BASEDIR/rewind_$how
	log_must mkdir -p $dir
	log_must raidz_epoch_test create $dir $POOL $WIDTH 1
	out=$(raidz_epoch_test write $dir $POOL 8192) ||
	    log_fail "write failed: $out"
	obj=$(print -r -- "$out" | sed -n 's/.* object=\([0-9]*\).*/\1/p')
	[[ $obj == +([0-9]) ]] || log_fail "no object in: $out"
	# Both rewind paths need a protected MOS root.  An arbitrary old
	# uberblock can point to metadata reused by the later setter.
	out=$(raidz_epoch_test checkpoint $dir $POOL) ||
	    log_fail "checkpoint failed: $out"
	log_note "$out"
	saved=$(print -r -- "$out" |
	    sed -n 's/.* txg=\([0-9]*\).*/\1/p')
	[[ $saved == +([0-9]) ]] && (( saved > 0 )) ||
	    log_fail "no checkpoint txg in: $out"
	out=$(zhack -d $dir raidz_epochs $POOL 0 0:$WIDTH:1 next:$WIDTH:2) ||
	    log_fail "zhack failed: $out"
	log_note "$out"
	start=$(print -r -- "$out" | sed -n "s/.* \([0-9]*\):$WIDTH:2\$/\1/p")
	[[ $start == +([0-9]) ]] || log_fail "no start in: $out"
	(( saved < start - 3 )) ||
	    log_fail "checkpoint $saved does not precede the setter: $out"
	to=$how
	[[ $how == txg ]] && to=$saved
	out=$(raidz_epoch_test crash $dir $POOL $to) ||
	    log_fail "the rewound import failed: $out"
	log_note "$out"
	first=$(print -r -- "$out" | sed -n 's/.* first=\([0-9]*\).*/\1/p')
	(( first > start )) ||
	    log_fail "the import does not write past $start: $out"
	count_values $dir label
	(( n == 0 )) ||
	    log_fail "the first txg of the rewound import kept the table"
	out=$(raidz_epoch_test read $dir $POOL $obj 8192) ||
	    log_fail "import after the crash failed: $out"
	log_note "$out"
	[[ $out == *"rc=0 match=1"* ]] || log_fail "read mismatch: $out"
done

boundary_case parity3 3 $WIDTH 2 0 refused
boundary_case parity4 4 $WIDTH 2 0 dropped
boundary_case width3 3 $((WIDTH - 1)) 1 0 refused
boundary_case same3 3 $WIDTH 1 0 dropped
boundary_case rewound 4 $WIDTH 2 1 recovered

log_pass "config-only epochs preserve data; ZAP epochs propagate normally"
