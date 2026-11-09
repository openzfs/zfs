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
# Copyright (c) 2026 Tigerblue77
#

. $STF_SUITE/tests/functional/anyraid/anyraid_common.kshlib

#
# DESCRIPTION:
# A copy of the AnyRAID tile map whose checksums are valid but whose
# header or entries are outside the limits of the format is rejected on
# import, and the next copy is used.
#
# STRATEGY:
# 1. Create an anymirror1 pool of three files with 64 MiB tiles, write
#    a file and export the pool.
# 2. Rewrite every copy of the map on every file with one invalid field,
#    and checksums that match: a length past the 64 MiB map whose fifth
#    chunk passes its checksum, the length stored as a signed integer,
#    a tile size of 0 and one smaller than the largest block, an offset
#    at the capacity of the child, a map that ends inside a tile, skips
#    that wrap the tile number, and a disk the vdev does not have.
# 3. Verify that "zpool import -d" lists the pool with the anymirror
#    unavailable and that the pool cannot be imported.
# 4. Rewrite every copy with a length of exactly 16 MiB, then only the
#    newest copy with a map that ends inside a tile, and verify each
#    time that the pool imports with the file intact.
#

verify_runnable "global"

DIR=$TEST_BASE_DIR/anyraid_import_bad_map
LISTING=$TEST_BASE_DIR/anyraid_import_bad_map.out
TMO=120

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -rf $DIR $LISTING
	set_tunable64 ANYRAID_MIN_TILE_SIZE 1073741824
}

#
# Rewrite every copy of the tile map on the three files of the exported
# pool, and sign it again as the kernel does: each chunk of the map with
# SHA-256, and the header with its label checksum (for an ashift up to
# 13).  The changes are applied in order to each copy:
#   length=N       set the length of the map in bytes
#   tile_size=N    set the tile size
#   append=E,...   append map entries, in hex: a skip is the count << 8,
#                  a location 1 | disk << 8 | offset << 16
#   truncate=N     drop the last N entries
#   wrap           append skips that wrap the tile number to 0, then
#                  the entries of tile 0
#   int32_length   store the length as an int32 instead of a uint32
#   chunk4         past 64 MiB, write a fifth chunk whose checksum is the
#                  label checksum of the header, so that it is read
#   newest         change only the copy with the highest txg
#
function craft_map # change...
{
	log_must python3 - "$@" -- $DIR/vdev.{0,1,2} <<-'EOF'
	import hashlib, os, struct, sys

	i = sys.argv.index("--")
	ops, files = sys.argv[1:i], sys.argv[i + 1:]
	LABEL_START, LABEL_END, COPY = 4 << 20, 512 << 10, 65 << 20
	HDR, ECK, CHUNK = 8192, 40, 16 << 20
	NVL = HDR - 5 * ECK
	MAGIC = struct.pack("=Q", 0x210da7ab10c7a11)

	def sha(b):
	    d = hashlib.sha256(b).digest()
	    return struct.pack("=4Q", *struct.unpack(">4Q", d))

	def field(h, name):
	    # Offset of the type of XDR nvpair 'name'; its value is 8 bytes on.
	    n = name.encode()
	    pad = -len(n) % 4
	    p = h.find(struct.pack(">I", len(n)) + n + b"\0" * pad)
	    assert p > 0, name
	    return p + 4 + len(n) + pad

	def fmt(h, t):
	    return {6: ">I", 8: ">Q"}[struct.unpack_from(">i", h, t)[0]]

	def get(h, name):
	    t = field(h, name)
	    return struct.unpack_from(fmt(h, t), h, t + 8)[0]

	def put(h, name, val):
	    t = field(h, name)
	    struct.pack_into(fmt(h, t), h, t + 8, val)

	def rnd(n, s):
	    return max(-(-n // s) * s, s)

	def entries(fd, m, n):
	    return struct.unpack("<%dI" % (n // 4), os.pread(fd, n, m))

	def wrap(fd, m, n):
	    # Skips that take the tile number round to 0, then tile 0 again.
	    tile, pat, first = 0, 0, []
	    for e in entries(fd, m, n):
	        if e & 0xff == 0:
	            tile += e >> 8
	            continue
	        tile += pat == 0
	        pat ^= 1
	        if tile == 1:
	            first.append(e)
	    assert tile < 256 and len(first) == 2
	    return [0xffffff00] * 256 + [(256 - tile) << 8] + first

	for f in files:
	    fd = os.open(f, os.O_RDWR)
	    psize = os.fstat(fd).st_size & ~((256 << 10) - 1)
	    hdrs = []
	    for o in (LABEL_START, LABEL_START + COPY,
	        psize - LABEL_END - 2 * COPY, psize - LABEL_END - COPY):
	        h = bytearray(os.pread(fd, HDR, o))
	        ver = MAGIC + struct.pack("=Q", o) + bytes(24)
	        if sha(h[:-ECK] + ver) == h[-32:]:
	            hdrs.append((o, h, ver))
	    assert hdrs, f
	    newest = max(get(h, "txg") for o, h, ver in hdrs)
	    for o, h, ver in hdrs:
	        if "newest" in ops and get(h, "txg") != newest:
	            continue
	        m = o + HDR
	        n = get(h, "length")
	        # The sector size the map was written with.
	        s = next(s for s in (512 << k for k in range(5))
	            if sha(os.pread(fd, rnd(n, s), m)) == h[NVL + 8:NVL + ECK])
	        for k, _, v in (op.partition("=") for op in ops):
	            new = []
	            if k == "length":
	                n = int(v, 0)
	            elif k == "tile_size":
	                put(h, k, int(v, 0))
	            elif k == "append":
	                new = [int(e, 16) for e in v.split(",")]
	            elif k == "truncate":
	                n -= 4 * int(v)
	            elif k == "wrap":
	                new = wrap(fd, m, n)
	            os.pwrite(fd, struct.pack("<%dI" % len(new), *new), m + n)
	            n += 4 * len(new)
	        put(h, "length", n)
	        nmaps = (n - 1) // CHUNK + 1 if n > 0 else 1
	        for i in range(min(nmaps, 4)):
	            b = os.pread(fd, rnd(min(n - i * CHUNK, CHUNK), s),
	                m + i * CHUNK)
	            h[NVL + i * ECK:NVL + (i + 1) * ECK] = MAGIC + sha(b)
	        if "int32_length" in ops:
	            struct.pack_into(">i", h, field(h, "length"), 5)
	        signed = h[:-ECK] + ver
	        h[-ECK:] = MAGIC + sha(signed)
	        os.pwrite(fd, h, o)
	        if "chunk4" in ops and nmaps > 4:
	            # Its checksum is the label checksum of the header.
	            assert rnd(n - 4 * CHUNK, s) == HDR
	            os.pwrite(fd, signed, m + 4 * CHUNK)
	    os.fsync(fd)
	    os.close(fd)
	EOF
}

#
# Create the pool, write a file and export the pool; then rewrite its
# maps with the given changes.
#
function bad_map # change...
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	log_must rm -rf $DIR
	log_must mkdir -p $DIR
	log_must truncate -s 768M $DIR/vdev.{0,1,2}
	log_must zpool create -f $TESTPOOL anymirror1 $DIR/vdev.{0,1,2}
	log_must file_write -o create -f /$TESTPOOL/file.bin -b 1048576 \
	    -c 32 -d R
	cksum=$(xxh128digest /$TESTPOOL/file.bin)
	log_must zpool export $TESTPOOL
	log_note "Map changes: $*"
	craft_map "$@"
}

log_onexit cleanup

log_assert "AnyRAID tile maps outside the format are rejected on import"

log_must set_tunable64 ANYRAID_MIN_TILE_SIZE 67108864

#
# Each child holds seven 64 MiB tiles, at offsets 0 to 6.  The appended
# tiles are numbered 1000 past the last one, which no I/O reaches.
#
for change in "length=0x4002000 chunk4" "int32_length" "tile_size=0" \
    "tile_size=0x800000" "append=3e800,00070101,00070201" "truncate=1" \
    "wrap" "append=3e800,00000301,00000101"; do
	bad_map $change
	log_must eval "timeout $TMO zpool import -d $DIR > $LISTING"
	cat $LISTING
	log_must grep -Eq "anymirror1-0 +UNAVAIL" $LISTING
	log_mustnot timeout $TMO zpool import -d $DIR $TESTPOOL
	log_mustnot poolexists $TESTPOOL
done

#
# A map of exactly 16 MiB, here padded with skips of 0, is read whole.
# A bad newest copy leaves the next one, written in the txg before it.
#
for change in "length=0x1000000" "newest truncate=1"; do
	bad_map $change
	log_must timeout $TMO zpool import -d $DIR $TESTPOOL
	log_must check_state $TESTPOOL "" "online"
	[[ $(xxh128digest /$TESTPOOL/file.bin) == "$cksum" ]] || \
	    log_fail "file.bin changed across the import"
	log_must zpool scrub -w $TESTPOOL
	log_must check_pool_status $TESTPOOL "errors" "No known data errors"
done

log_pass "AnyRAID tile maps outside the format are rejected on import"
