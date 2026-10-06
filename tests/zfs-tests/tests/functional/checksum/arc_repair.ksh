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

verify_runnable "global"

typeset pool=$TESTPOOL2
typeset vdev="$TESTDIR/arc_repair.vdev"
typeset original="$TESTDIR/arc_repair.original"
typeset file="$TESTDIR/arc_repair/data"
typeset legacy=$(get_tunable SCAN_LEGACY)
typeset carc=$(get_tunable COMPRESSED_ARC_ENABLED)
typeset injection

function cleanup
{
	[[ -n $injection ]] && log_must zinject -c $injection
	log_must set_tunable32 SCAN_LEGACY $legacy
	log_must set_tunable64 COMPRESSED_ARC_ENABLED $carc
	destroy_pool $pool
	rm -f "$vdev" "$original"
}
log_onexit cleanup

function check_repaired
{
	log_must check_pool_status $pool scan "with 0 errors" true
	log_must check_pool_status $pool errors "No known data errors" true
}

log_assert "Scrub repairs disk corruption from a verified ARC copy"

log_must set_tunable64 COMPRESSED_ARC_ENABLED 1
log_must truncate -s $MINVDEVSIZE "$vdev"
log_must zpool create -f -O mountpoint=none $pool "$vdev"
log_must dd if=/dev/urandom of="$original" bs=64k count=1
log_must dd if=/dev/zero of="$original" bs=64k count=1 seek=1 conv=notrunc

for scan_mode in 0 1; do
	log_must set_tunable32 SCAN_LEGACY $scan_mode
	for copies in 1 2 3; do
		for compression in off lz4; do
			log_note "legacy=$scan_mode copies=$copies compression=$compression"
			log_must zfs create -o mountpoint="$TESTDIR/arc_repair" \
			    -o copies=$copies -o compression=$compression \
			    -o checksum=fletcher4 $pool/fs
			log_must cp "$original" "$file"
			log_must sync_pool $pool
			log_must cat "$file" > /dev/null
			log_must corrupt_blocks_at_level "$file"
			log_must zpool scrub -w $pool
			check_repaired
			log_mustnot check_pool_status $pool scan "repaired 0B" true

			# Drop ARC so neither reads nor the next scrub can repair again.
			log_must zpool export $pool
			log_must zpool import -d "$vdev" $pool
			log_must zfs set primarycache=metadata $pool/fs
			log_must cmp "$original" "$file"
			log_must zpool scrub -w $pool
			check_repaired
			log_must check_pool_status $pool scan "repaired 0B" true
			log_must zfs destroy $pool/fs
		done
	done
done

# ARC must not hide thorough scrub errors or repair unchecked data.
for fault in decompress io; do
	typeset scrub_flags=-t
	typeset checksum=fletcher4
	if [[ $fault == io ]]; then
		scrub_flags=
		checksum=off
	fi
	log_must zfs create -o mountpoint="$TESTDIR/arc_repair" \
		-o compression=lz4 -o checksum=$checksum $pool/fs
	log_must cp "$original" "$file"
	log_must sync_pool $pool
	log_must cat "$file" > /dev/null
	injection=$(zinject -q -t data -e $fault "$file") ||
		log_fail "Could not inject a $fault error"
	log_must zpool scrub $scrub_flags -w $pool
	log_must check_pool_status $pool scan "with [1-9][0-9]* errors" true
	log_must check_pool_status $pool scan "repaired 0B" true
	log_must zinject -c $injection
	injection=
	log_must zfs destroy $pool/fs
done

# A missing ARC copy must leave the corruption visible.
log_must zfs create -o mountpoint="$TESTDIR/arc_repair" \
	-o primarycache=metadata -o compression=off $pool/fs
log_must cp "$original" "$file"
log_must corrupt_blocks_at_level "$file"
log_must zpool export $pool
log_must zpool import -d "$vdev" $pool
log_must zpool scrub -w $pool
log_must check_pool_status $pool scan "with [1-9][0-9]* errors" true
log_mustnot check_pool_status $pool errors "No known data errors" true
log_must check_pool_status $pool scan "repaired 0B" true

log_pass "Scrub repaired disk corruption only when a valid ARC copy existed"
