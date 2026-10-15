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
#	zdb -R reads a block of a raidz vdev with the layout of the
#	transaction group the t flag gives, and with the first layout of the
#	vdev without it.
#
# STRATEGY:
#	1. Create a raidz1 pool on file vdevs, and give its vdev a parity-epoch
#	   table whose second epoch has parity 2.
#	2. Write a block of incompressible data, born in that epoch, and note
#	   its offset, size, birth and checksum from zdb -ddddd.
#	3. Require zdb -R without the t flag to read other data than the
#	   block's, as laid out with parity 1.
#	4. Require zdb -R with the t flag and the birth of the block to read
#	   the block, whose fletcher4 checksum is then the one of its block
#	   pointer.
#	5. Require zdb -R to refuse a birth that is not only decimal digits.
#

verify_runnable "global"

typeset -r BASEDIR=$TEST_BASE_DIR/zdb_read_block_birth.$$
typeset -r POOL=${TESTPOOL}_zdbr
typeset out obj birth start line dva off psize cksum got
typeset -a f

function cleanup
{
	rm -rf $BASEDIR
}

# A checksum as zdb prints it, without leading zeros.
function strip_zeros
{
	sed 's/^0*\([0-9a-f]\)/\1/; s/:0*\([0-9a-f]\)/:\1/g'
}

# Set got to the fletcher4 checksum of what zdb -R reads.
function read_cksum
{
	log_must eval "zdb -e -p $BASEDIR -R $POOL $1 > $BASEDIR/read.out 2>&1"
	got=$(awk '$1 == "fletcher4" { sub("cksum=", "", $2); print $2 }' \
	    $BASEDIR/read.out | strip_zeros)
	[[ -n $got ]] || log_fail "no fletcher4 checksum from zdb -R $1"
}

log_onexit cleanup
log_assert "zdb -R reads a raidz block with the layout of the txg it is given"

log_must mkdir -p $BASEDIR
log_must raidz_epoch_test create $BASEDIR $POOL 5 1
out=$(zhack -d $BASEDIR raidz_epochs $POOL 0 0:5:1 next:5:2) ||
    log_fail "zhack could not store the table: $out"
log_note "$out"
start=${out##* }
start=${start%%:*}

out=$(raidz_epoch_test put $BASEDIR $POOL 65536) ||
    log_fail "put failed: $out"
log_note "$out"
obj=$(print -r -- "$out" | sed -n 's/.* object=\([0-9]*\).*/\1/p')
birth=$(print -r -- "$out" | sed -n 's/.* birth=\([0-9]*\).*/\1/p')
[[ $obj == +([0-9]) && $birth == +([0-9]) && $start == +([0-9]) ]] ||
    log_fail "no object, birth or start"
(( birth >= start )) || log_fail "block born in $birth, before epoch $start"

log_must eval "zdb -e -p $BASEDIR -ddddd $POOL/ $obj > $BASEDIR/obj.out"
line=$(awk '$2 == "L0" && $3 ~ /^0:/' $BASEDIR/obj.out | head -1)
[[ -n $line ]] || log_fail "no level-0 block pointer for object $obj"
log_note "$line"
set -A f $line
dva=${f[2]#0:}
off=${dva%%:*}
psize=${f[3]#*L/}
psize=${psize%P}
cksum=$(print -r -- "${line##*cksum=}" | strip_zeros)
[[ $off == +([0-9a-f]) && $psize == +([0-9a-f]) && -n $cksum ]] ||
    log_fail "cannot parse the block pointer: $line"

read_cksum 0:$off:$psize:c
[[ $got != "$cksum" ]] ||
    log_fail "without t, zdb -R read the block with the layout of its epoch"

read_cksum 0:$off:$psize:ct$birth
[[ $got == "$cksum" ]] ||
    log_fail "zdb -R with t$birth read $got, the block has $cksum"

# strtoull() alone would read t-1 as the last txg there is, and take a sign
# or spaces before the digits.
for t in "t-1" "t+$birth" "t $birth" "tx"; do
	log_must eval "zdb -e -p $BASEDIR -R $POOL '0:$off:$psize:c$t' \
	    > $BASEDIR/bad.out 2>&1"
	grep -q "Invalid birth txg" $BASEDIR/bad.out ||
	    log_fail "zdb -R took the birth in c$t: $(head -3 $BASEDIR/bad.out)"
done

log_pass "zdb -R reads a raidz block with the layout of the txg it is given"
