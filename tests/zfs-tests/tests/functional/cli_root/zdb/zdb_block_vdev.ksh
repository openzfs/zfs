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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_load-key/zfs_load-key_common.kshlib

#
# DESCRIPTION:
#	'zdb -b --vdev=<vdev>' reports what holds the space allocated on
#	one top-level vdev, and the groups add up to what the vdev's space
#	maps record as allocated.
#
# STRATEGY:
#	1. On a raidz1 pool with a special mirror, write files, take a
#	   snapshot and clone a file: the block cloning and snapshot rows
#	   are filled, every way of naming the vdev gives the same report,
#	   and both vdevs balance.
#	2. Bad arguments are rejected.
#	3. The rest of the output is unchanged by --vdev.
#	4. Take a checkpoint, stop allocating on the special mirror and
#	   rewrite the files, skipping cloned and snapshot blocks: the
#	   checkpoint row fills, the cloned and snapshot rows stay, the
#	   live row shrinks, and the vdev balances with and without -L.
#	   Rewriting a file without skipping snapshot blocks moves them to
#	   the snapshot-only row.
#	5. Discard the checkpoint, destroy the snapshot, remove the clone
#	   and rewrite: each of those rows empties.
#	6. With log space maps kept at export, the vdevs balance with and
#	   without -L.
#	7. On a pool of three plain vdevs, force gang blocks, also in an
#	   encrypted dataset, before and after dynamic_gang_header becomes
#	   active, so that gang headers of both sizes exist: every vdev
#	   balances.
#	8. Remove a vdev: the others balance and the removed one is
#	   rejected.
#	9. On a pool whose device is named "s0", an unknown name is
#	   rejected.
#	10. In a dataset whose blocks all sit on the special mirror, take
#	   three snapshots with frees between them: with -bb, each
#	   snapshot-only row is what "zfs destroy -nv" would reclaim, they
#	   add up to usedbysnapshots, and the live rows to referenced.
#	11. Clone the middle snapshot: the rows name the clone, and no
#	   snapshot-only range includes its origin.
#

verify_runnable "global"

if ! command -v clonefile > /dev/null ; then
	log_unsupported "clonefile program required to test block cloning"
fi

VDEV_DIR=$TEST_BASE_DIR/zdb_block_vdev
OUT=$VDEV_DIR/out
SNAP_ROWS="born in snapshots (live and/or snapshot-only; use -bb to split)"

function cleanup
{
	typeset pool

	for pool in $TESTPOOL $TESTPOOL1 $TESTPOOL2; do
		poolexists $pool || zpool import -d $VDEV_DIR $pool \
		    >/dev/null 2>&1
		poolexists $pool && destroy_pool $pool
	done
	rm -rf $VDEV_DIR
	restore_tunable METASLAB_FORCE_GANGING
	restore_tunable METASLAB_FORCE_GANGING_PCT
	restore_tunable KEEP_LOG_SPACEMAPS_AT_EXPORT
	restore_tunable BCLONE_ENABLED
}

function export_pool # pool
{
	sync_pool $1 true
	log_must zpool wait -t free $1
	log_must zpool export $1
}

function import_pool # pool
{
	log_must zpool import -d $VDEV_DIR $1
}

# Run zdb with --vdev on an exported pool: zdbv pool vdev file [flags]
function zdbv
{
	typeset pool=$1
	typeset vdev=$2
	typeset file=$3
	shift 3

	log_must eval "zdb -e -p $VDEV_DIR $* --vdev=$vdev $pool > $file"
}

# The ASIZE of the row with exactly this label, or 0: row file label
function row
{
	awk -v l="$2" '
	    NF >= 3 && substr($0, index($0, $3)) == l { v = $2; found = 1 }
	    END { print (found ? v : 0) }' $1
}

# The value of a closing line: total file label
function total
{
	awk -v l="$2" '
	    NF >= 2 && substr($0, index($0, $2)) == l { print $1 }' $1
}

# The report balances: "not accounted for" is zero (0B without -P)
function balanced # file
{
	typeset left

	left=$(total $1 "not accounted for")
	if [[ "$left" != "0" && "$left" != "0B" ]]; then
		cat $1
		log_fail "$1: $left not accounted for"
	fi
}

# The report section alone
function section # file
{
	sed -n '/^Blocks on vdev /,/not accounted for$/p' $1
}

# The output without the report section and its notes
function without_section # file
{
	awk '/^Blocks on vdev / { skip = 1; next }
	    skip && (/^\t/ || /^$/) { next }
	    { skip = 0; print }' $1
}

log_onexit cleanup

log_assert "'zdb -b --vdev' accounts for the space allocated on a vdev"

log_must mkdir -p $VDEV_DIR
log_must truncate -s $MINVDEVSIZE $VDEV_DIR/dev-0 $VDEV_DIR/dev-1 \
    $VDEV_DIR/dev-2 $VDEV_DIR/dev-3 $VDEV_DIR/dev-4 $VDEV_DIR/dev-5 \
    $VDEV_DIR/dev-6 $VDEV_DIR/dev-7 $VDEV_DIR/s0

if tunable_exists BCLONE_ENABLED ; then
	log_must save_tunable BCLONE_ENABLED
	log_must set_tunable32 BCLONE_ENABLED 1
fi

#
# 1. Basics: vdev 1 is the special mirror, where all of fs's blocks go.
#
log_must zpool create -f -o feature@block_cloning=enabled $TESTPOOL \
    raidz1 $VDEV_DIR/dev-0 $VDEV_DIR/dev-1 $VDEV_DIR/dev-2 \
    special mirror $VDEV_DIR/dev-3 $VDEV_DIR/dev-4
log_must zfs create -o recordsize=32K -o special_small_blocks=32K \
    -o compression=off $TESTPOOL/fs
mnt=$(get_prop mountpoint $TESTPOOL/fs)
log_must dd if=/dev/urandom of=$mnt/A bs=1M count=8
log_must dd if=/dev/urandom of=$mnt/B bs=1M count=8
sync_pool $TESTPOOL true
log_must zfs snapshot $TESTPOOL/fs@s
log_must dd if=/dev/urandom of=$mnt/B bs=1M count=4 conv=notrunc
log_must clonefile -f $mnt/A $mnt/C

guid=$(zpool get -Hpo value guid $TESTPOOL mirror-1)
pool_guid=$(zpool get -Hpo value guid $TESTPOOL)
export_pool $TESTPOOL
zdbv $TESTPOOL 1 $OUT.1 -bP
log_must test "$(row $OUT.1 "block cloning (BRT)")" -gt 0
log_must test "$(row $OUT.1 "$TESTPOOL/fs: $SNAP_ROWS")" -gt 0
log_must test "$(row $OUT.1 "$TESTPOOL/fs: live, in no snapshot")" -gt 0
balanced $OUT.1
zdbv $TESTPOOL 0 $OUT.0 -bP
balanced $OUT.0
section $OUT.0 > $OUT.ref
zdbv $TESTPOOL raidz1-0 $OUT.name -bP
section $OUT.name > $OUT.cmp
log_must diff $OUT.ref $OUT.cmp
section $OUT.1 > $OUT.ref
for vdev in mirror-1 dev-3 $VDEV_DIR/dev-3 $guid; do
	zdbv $TESTPOOL $vdev $OUT.name -bP
	section $OUT.name > $OUT.cmp
	log_must diff $OUT.ref $OUT.cmp
done

#
# 2. Bad arguments.
#
log_mustnot zdb -e -p $VDEV_DIR -d --vdev=1 $TESTPOOL
log_mustnot zdb -e -p $VDEV_DIR -b --vdev=9 $TESTPOOL
log_mustnot zdb -e -p $VDEV_DIR -b --vdev=nosuch $TESTPOOL
log_mustnot zdb -e -p $VDEV_DIR -b --vdev=mirrorXYZ-1 $TESTPOOL
log_mustnot zdb -e -p $VDEV_DIR -b --vdev=raidz3-0 $TESTPOOL
log_mustnot zdb -e -p $VDEV_DIR -b --vdev= $TESTPOOL
log_mustnot zdb -e -p $VDEV_DIR -b --vdev=$pool_guid $TESTPOOL
log_mustnot zdb -e -p $VDEV_DIR -b --vdev=1 $TESTPOOL/fs
log_mustnot zdb -e -p $VDEV_DIR -bd --vdev=1 $TESTPOOL 1
log_mustnot zdb -e -p $VDEV_DIR -bS --vdev=1 $TESTPOOL

#
# 3. The rest of the output is unchanged.
#
for flags in -b -bb; do
	log_must eval "zdb -e -p $VDEV_DIR $flags $TESTPOOL > $OUT.plain"
	zdbv $TESTPOOL 1 $OUT.vdev $flags
	without_section $OUT.vdev > $OUT.stripped
	log_must diff $OUT.plain $OUT.stripped
done
import_pool $TESTPOOL
mnt=$(get_prop mountpoint $TESTPOOL/fs)

#
# 4. Emptying the special mirror.
#
brt=$(row $OUT.1 "block cloning (BRT)")
snaps=$(row $OUT.1 "$TESTPOOL/fs: $SNAP_ROWS")
live=$(row $OUT.1 "$TESTPOOL/fs: live, in no snapshot")
log_must zpool checkpoint $TESTPOOL
log_must zpool set allocating=off $TESTPOOL mirror-1
log_must zfs rewrite -r -C -S $mnt
export_pool $TESTPOOL
zdbv $TESTPOOL 1 $OUT.1 -bP
log_must test "$(row $OUT.1 "checkpoint")" -gt 0
log_must test "$(row $OUT.1 "block cloning (BRT)")" -eq $brt
log_must test "$(row $OUT.1 "$TESTPOOL/fs: $SNAP_ROWS")" -eq $snaps
log_must test "$(row $OUT.1 "$TESTPOOL/fs: live, in no snapshot")" -lt $live
balanced $OUT.1
zdbv $TESTPOOL 1 $OUT.1L -bPL
balanced $OUT.1L
zdbv $TESTPOOL 1 $OUT.1bb -bbP
only=$(row $OUT.1bb "$TESTPOOL/fs: only in snapshots $TESTPOOL/fs@s")
import_pool $TESTPOOL
mnt=$(get_prop mountpoint $TESTPOOL/fs)

# The 128 blocks of B that the snapshot shares are left to it alone.
log_must zfs rewrite $mnt/B
export_pool $TESTPOOL
zdbv $TESTPOOL 1 $OUT.1bb -bbP
log_must test \
    "$(row $OUT.1bb "$TESTPOOL/fs: only in snapshots $TESTPOOL/fs@s")" \
    -ge $((only + 128 * 32768))
balanced $OUT.1bb
import_pool $TESTPOOL

#
# 5. Releasing what holds the rest.
#
log_must zpool checkpoint -dw $TESTPOOL
log_must zfs destroy $TESTPOOL/fs@s
export_pool $TESTPOOL
zdbv $TESTPOOL 1 $OUT.1 -bP
log_must test "$(row $OUT.1 "checkpoint")" -eq 0
log_must test "$(row $OUT.1 "$TESTPOOL/fs: $SNAP_ROWS")" -eq 0
balanced $OUT.1
zdbv $TESTPOOL 1 $OUT.1bb -bbP
log_mustnot grep -q "$TESTPOOL/fs: only in snapshots" $OUT.1bb
import_pool $TESTPOOL
mnt=$(get_prop mountpoint $TESTPOOL/fs)
log_must rm $mnt/C
sync_pool $TESTPOOL true
log_must zpool wait -t free $TESTPOOL
log_must zfs rewrite $mnt/A
export_pool $TESTPOOL
zdbv $TESTPOOL 1 $OUT.1 -bP
log_must test "$(row $OUT.1 "block cloning (BRT)")" -eq 0
balanced $OUT.1
import_pool $TESTPOOL

#
# 6. Unflushed log space map entries at export.
#
log_must save_tunable KEEP_LOG_SPACEMAPS_AT_EXPORT
log_must set_tunable64 KEEP_LOG_SPACEMAPS_AT_EXPORT 1
mnt=$(get_prop mountpoint $TESTPOOL/fs)
log_must zpool set allocating=on $TESTPOOL mirror-1
log_must dd if=/dev/urandom of=$mnt/D bs=1M count=4
export_pool $TESTPOOL
for vdev in 0 1; do
	zdbv $TESTPOOL $vdev $OUT.u -bP
	balanced $OUT.u
	zdbv $TESTPOOL $vdev $OUT.uL -bPL
	balanced $OUT.uL
done
log_must restore_tunable KEEP_LOG_SPACEMAPS_AT_EXPORT

#
# 7. Gang blocks, whose members may sit on another vdev than their header.
# A gang header written before dynamic_gang_header becomes active keeps the
# old size of 512 bytes even once the feature is active, and with ashift 12
# the headers written after it take 4K.
#
log_must zpool create -f -o ashift=12 \
    -o feature@dynamic_gang_header=disabled $TESTPOOL1 \
    $VDEV_DIR/dev-5 $VDEV_DIR/dev-6 $VDEV_DIR/dev-7
log_must zfs create -o recordsize=128K -o compression=off $TESTPOOL1/fs
log_must eval "echo $PASSPHRASE | zfs create -o encryption=on" \
    "-o keyformat=passphrase -o recordsize=128K -o compression=off" \
    "$TESTPOOL1/enc"
log_must save_tunable METASLAB_FORCE_GANGING
log_must save_tunable METASLAB_FORCE_GANGING_PCT
log_must set_tunable64 METASLAB_FORCE_GANGING 16384
log_must set_tunable32 METASLAB_FORCE_GANGING_PCT 100
fsmnt=$(get_prop mountpoint $TESTPOOL1/fs)
log_must dd if=/dev/urandom of=$fsmnt/F bs=1M count=32
mnt=$(get_prop mountpoint $TESTPOOL1/enc)
log_must dd if=/dev/urandom of=$mnt/F bs=1M count=8
sync_pool $TESTPOOL1 true
# The feature becomes active when the members of a gang block do not fit in
# the three that an old header holds while a larger header was possible,
# which forced ganging causes.
log_must zpool set feature@dynamic_gang_header=enabled $TESTPOOL1
log_must dd if=/dev/urandom of=$fsmnt/G bs=1M count=8
sync_pool $TESTPOOL1 true
log_must zpool get feature@dynamic_gang_header $TESTPOOL1
log_must test "$(get_pool_prop feature@dynamic_gang_header $TESTPOOL1)" = \
    "active"
log_must dd if=/dev/urandom of=$fsmnt/H bs=1M count=8
log_must dd if=/dev/urandom of=$mnt/H bs=1M count=8
sync_pool $TESTPOOL1 true
log_must restore_tunable METASLAB_FORCE_GANGING
log_must restore_tunable METASLAB_FORCE_GANGING_PCT
export_pool $TESTPOOL1

log_must eval "zdb -e -p $VDEV_DIR -b $TESTPOOL1 > $OUT.g"
ganged=$(awk '/ganged count:/ { print $3 }' $OUT.g)
log_must test "$ganged" -gt 0

# Each DVA's asize, summed by vdev as if gang members sat with their header
log_must eval "zdb -e -p $VDEV_DIR -bbbbb $TESTPOOL1 > $OUT.bp"
cross=0
for vdev in 0 1 2; do
	zdbv $TESTPOOL1 $vdev $OUT.g$vdev -bP
	balanced $OUT.g$vdev
	naive=$(awk -v v=$vdev '
	    function hex(s,  i, n) {
		n = 0
		s = tolower(s)
		for (i = 1; i <= length(s); i++)
			n = n * 16 + index("0123456789abcdef", \
			    substr(s, i, 1)) - 1
		return (n)
	    }
	    {
		while (match($0, /DVA\[[0-9]\]=<[0-9]+:[0-9a-f]+:[0-9a-f]+>/)) {
			d = substr($0, RSTART + 8, RLENGTH - 9)
			$0 = substr($0, RSTART + RLENGTH)
			split(d, f, ":")
			if (f[1] == v && !((f[2]) in seen)) {
				seen[f[2]] = 1
				sum += hex(f[3])
			}
		}
	    }
	    END { print sum + 0 }' $OUT.bp)
	allocated=$(total $OUT.g$vdev "allocated (space maps)")
	log_note "vdev $vdev: allocated $allocated, by header vdev $naive"
	[[ "$naive" != "$allocated" ]] && cross=1
done
if [[ $cross -eq 0 ]]; then
	log_note "no gang member was allocated on another vdev than its" \
	    "header; that case was not exercised"
fi

#
# 8. Removal: the copies on the removed vdev are counted where they went.
#
import_pool $TESTPOOL1
log_must zpool remove $TESTPOOL1 $VDEV_DIR/dev-5
log_must zpool wait -t remove $TESTPOOL1
export_pool $TESTPOOL1
# Without -P: "zdb -bP" prints the size of an indirect mapping through a
# buffer length that is not the buffer's.
for vdev in 1 2; do
	zdbv $TESTPOOL1 $vdev $OUT.r$vdev -b
	balanced $OUT.r$vdev
done
log_mustnot zdb -e -p $VDEV_DIR -b --vdev=0 $TESTPOOL1
log_mustnot zdb -e -p $VDEV_DIR -b --vdev=dev-5 $TESTPOOL1

#
# 9. A leaf named "s0" must not match an unknown name.
#
log_must zpool create -f $TESTPOOL2 $VDEV_DIR/s0
export_pool $TESTPOOL2
zdbv $TESTPOOL2 s0 $OUT.s0 -bP
balanced $OUT.s0
log_mustnot zdb -e -p $VDEV_DIR -b --vdev=nosuch $TESTPOOL2

#
# 10. The snapshot rows against ZFS's own accounting. All of fs2's blocks
# sit on the special mirror, whose deflated size is its allocated size.
#
import_pool $TESTPOOL
fs2=$TESTPOOL/fs2
log_must zfs create -o recordsize=32K -o special_small_blocks=32K \
    -o compression=off $fs2
mnt=$(get_prop mountpoint $fs2)
log_must dd if=/dev/urandom of=$mnt/F1 bs=1M count=4
sync_pool $TESTPOOL true
log_must zfs snapshot $fs2@a
log_must dd if=/dev/urandom of=$mnt/F1 bs=1M count=1 conv=notrunc
log_must dd if=/dev/urandom of=$mnt/F2 bs=1M count=1
sync_pool $TESTPOOL true
log_must zfs snapshot $fs2@b
log_must rm $mnt/F2
sync_pool $TESTPOOL true
log_must zfs snapshot $fs2@c
log_must dd if=/dev/urandom of=$mnt/F1 bs=1M count=1 seek=1 conv=notrunc
sync_pool $TESTPOOL true
usedsnap=$(get_prop usedbysnapshots $fs2)
referenced=$(get_prop referenced $fs2)
export_pool $TESTPOOL
zdbv $TESTPOOL 1 $OUT.o -bbP
balanced $OUT.o
log_mustnot grep -q "^	note: inconsistent" $OUT.o
import_pool $TESTPOOL

prefix="$fs2: only in snapshots "
ranges=$(awk -v p="$prefix" 'NF >= 3 &&
    index(substr($0, index($0, $3)), p) == 1 {
	print substr($0, index($0, $3) + length(p))
    }' $OUT.o)
log_must test -n "$ranges"
sum=0
for range in $ranges; do
	value=$(row $OUT.o "$prefix$range")
	reclaim=$(zfs destroy -nvp $range | awk '/^reclaim/ { print $2 }')
	log_note "$range: $value on the vdev, $reclaim to reclaim"
	log_must test "$value" -eq "$reclaim"
	sum=$((sum + value))
done
log_must test $sum -eq $usedsnap
live=$(($(row $OUT.o "$fs2: live, in no snapshot") + \
    $(row $OUT.o "$fs2: live, also in snapshots")))
log_must test $live -eq $referenced

#
# 11. A clone of the middle snapshot.
#
log_must zfs clone $fs2@b ${fs2}c
mnt=$(get_prop mountpoint ${fs2}c)
log_must dd if=/dev/urandom of=$mnt/G bs=1M count=1
export_pool $TESTPOOL
zdbv $TESTPOOL 1 $OUT.c -bbP
balanced $OUT.c
log_must test "$(row $OUT.c \
    "$fs2: live or held through clones of $fs2@b (${fs2}c)")" -gt 0
log_mustnot grep -Eq "$fs2: only in snapshots ($fs2@b|.*%(b|c)$)" $OUT.c
log_must test "$(row $OUT.c "${fs2}c: live, in no snapshot")" -gt 0

log_pass "'zdb -b --vdev' accounts for the space allocated on a vdev"
