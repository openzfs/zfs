// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */

/*
 * Copyright (c) 2013, 2016 by Delphix. All rights reserved.
 */

#include <sys/blkptr.h>
#include <sys/zfs_context.h>
#include <sys/zio.h>
#include <sys/zio_compress.h>
#include <sys/zio_checksum.h>

/*
 * Embedded-data Block Pointers
 *
 * Normally, block pointers point (via their DVAs) to a block which holds data.
 * If the data that we need to store is very small, this is an inefficient
 * use of space, because a block must be at minimum 1 sector (typically 512
 * bytes or 4KB).  Additionally, reading these small blocks tends to generate
 * more random reads.
 *
 * Embedded-data Block Pointers allow small pieces of data (the "payload",
 * up to 112 bytes) to be stored in the block pointer itself, instead of
 * being pointed to.  The "Pointer" part of this name is a bit of a
 * misnomer, as nothing is pointed to.
 *
 * BP_EMBEDDED_TYPE_DATA block pointers allow highly-compressible data to
 * be embedded in the block pointer.  The logic for this is handled in
 * the SPA, by the zio pipeline.  Therefore most code outside the zio
 * pipeline doesn't need special-cases to handle these block pointers.
 *
 * See spa.h for details on the exact layout of embedded block pointers.
 */

void
encode_embedded_bp_compressed(blkptr_t *bp, void *data,
    enum zio_compress comp, int uncompressed_size, int compressed_size)
{
	uint64_t *bp64 = (uint64_t *)bp;
	uint64_t w = 0;
	uint8_t *data8 = data;

	ASSERT3U(compressed_size, <=, BPE_PAYLOAD_SIZE);
	ASSERT(uncompressed_size == compressed_size ||
	    comp != ZIO_COMPRESS_OFF);
	ASSERT3U(comp, >=, ZIO_COMPRESS_OFF);
	ASSERT3U(comp, <, ZIO_COMPRESS_FUNCTIONS);

	memset(bp, 0, sizeof (*bp));
	BP_SET_EMBEDDED(bp, B_TRUE);
	BP_SET_COMPRESS(bp, comp);
	BP_SET_BYTEORDER(bp, ZFS_HOST_BYTEORDER);
	BPE_SET_LSIZE(bp, uncompressed_size);
	BPE_SET_PSIZE(bp, compressed_size);

	/*
	 * Encode the byte array into the words of the block pointer.
	 * First byte goes into low bits of first word (little endian).
	 */
	for (int i = 0; i < compressed_size; i++) {
		BF64_SET(w, (i % sizeof (w)) * NBBY, NBBY, data8[i]);
		if (i % sizeof (w) == sizeof (w) - 1) {
			/* we've reached the end of a word */
			ASSERT3P(bp64, <, bp + 1);
			*bp64 = w;
			bp64++;
			if (!BPE_IS_PAYLOADWORD(bp, bp64))
				bp64++;
			w = 0;
		}
	}
	/* write last partial word */
	if (bp64 < (uint64_t *)(bp + 1))
		*bp64 = w;
}

/*
 * buf must be at least BPE_GET_PSIZE(bp) bytes long (which will never be
 * more than BPE_PAYLOAD_SIZE bytes).
 */
void
decode_embedded_bp_compressed(const blkptr_t *bp, void *buf)
{
	int psize;
	uint8_t *buf8 = buf;
	uint64_t w = 0;
	const uint64_t *bp64 = (const uint64_t *)bp;

	ASSERT(BP_IS_EMBEDDED(bp));

	psize = BPE_GET_PSIZE(bp);

	/*
	 * Decode the words of the block pointer into the byte array.
	 * Low bits of first word are the first byte (little endian).
	 */
	for (int i = 0; i < psize; i++) {
		if (i % sizeof (w) == 0) {
			/* beginning of a word */
			ASSERT3P(bp64, <, bp + 1);
			w = *bp64;
			bp64++;
			if (!BPE_IS_PAYLOADWORD(bp, bp64))
				bp64++;
		}
		buf8[i] = BF64_GET(w, (i % sizeof (w)) * NBBY, NBBY);
	}
}

/*
 * Fill in the buffer with the (decompressed) payload of the embedded
 * blkptr_t.  Takes into account compression and byteorder (the payload is
 * treated as a stream of bytes).
 * Return 0 on success, or ENOSPC if it won't fit in the buffer.
 */
int
decode_embedded_bp(const blkptr_t *bp, void *buf, int buflen)
{
	int lsize, psize;

	ASSERT(BP_IS_EMBEDDED(bp));

	lsize = BPE_GET_LSIZE(bp);
	psize = BPE_GET_PSIZE(bp);

	if (lsize > buflen)
		return (SET_ERROR(ENOSPC));
	ASSERT3U(lsize, ==, buflen);

	if (BP_GET_COMPRESS(bp) != ZIO_COMPRESS_OFF) {
		uint8_t dstbuf[BPE_PAYLOAD_SIZE];
		decode_embedded_bp_compressed(bp, dstbuf);
		abd_t cabd, dabd;
		abd_get_from_buf_struct(&cabd, dstbuf, psize);
		abd_get_from_buf_struct(&dabd, buf, buflen);
		VERIFY0(zio_decompress_data(BP_GET_COMPRESS(bp), &cabd,
		    &dabd, psize, buflen, NULL));
		abd_free(&dabd);
		abd_free(&cabd);
	} else {
		ASSERT3U(lsize, ==, psize);
		decode_embedded_bp_compressed(bp, buf);
	}

	return (0);
}

/*
 * Decide, from the two L0 block pointers alone, whether they already name the
 * same data: the same physical block, or two holes.  This is what a
 * previously cloned or deduped pair of ranges looks like, and it is the one
 * case where the clone step itself would have nothing left to do.
 */
boolean_t
zfs_dedupe_bp_same_block(const blkptr_t *bi, const blkptr_t *bo)
{
	/*
	 * Two holes are both runs of zeros, so they are equal without
	 * either one naming any storage, whatever size their pointers
	 * decode to.  Their sizes must not be compared.  A block that
	 * was never written comes back from dmu_read_l0_bps() as an
	 * all-zero block pointer, and its BP_GET_LSIZE() decodes to
	 * the minimum block size, not to the size of the block it
	 * stands for.  Returning here saves reading two holes to
	 * compare their zeros.
	 */
	if (BP_IS_HOLE(bi) && BP_IS_HOLE(bo))
		return (B_TRUE);

	if (BP_IS_HOLE(bi) || BP_IS_HOLE(bo) ||
	    BP_IS_EMBEDDED(bi) || BP_IS_EMBEDDED(bo) ||
	    BP_IS_REDACTED(bi) || BP_IS_REDACTED(bo))
		return (B_FALSE);

	/*
	 * One allocation is one DVA born in one txg, so a matching first DVA
	 * at a matching physical birth means both pointers name the same
	 * physical block - typically because the ranges were already cloned
	 * or deduped.  This needs no checksum and holds even where the
	 * checksum test below cannot (encryption, weak checksums): the same
	 * block under the same key decrypts to the same data.  BP_EQUAL() is
	 * too strict for this: it also compares the logical births, and
	 * cloning restamps those on every clone.
	 */
	return (BP_GET_PHYSICAL_BIRTH(bi) == BP_GET_PHYSICAL_BIRTH(bo) &&
	    DVA_EQUAL(&bi->blk_dva[0], &bo->blk_dva[0]));
}

/*
 * Decide, from the two L0 block pointers alone, whether the blocks provably
 * hold identical logical data.  Modeled on the nopwrite guard set in
 * zio_nop_write(): a match of a cryptographically strong (dedup-grade)
 * checksum is trusted to imply data equality, exactly as ZFS dedup relies on
 * it.  This can only ever prove equality; a mismatch (or any non-provable
 * case) is inconclusive and the caller must fall back to reading the data.
 * Embedded blocks are the exception to needing a checksum: their payload
 * lives in the pointer itself, so two of them are compared directly.
 *
 * All of the following must hold for a checksum match to be trusted:
 *   - neither BP is embedded, a hole, or redacted (no usable blk_cksum);
 *   - neither BP is encrypted (per-block IV/salt make the checksum data-
 *     independent, so equal plaintext yields different checksums);
 *   - both BPs use the same checksum function and it carries the DEDUP flag
 *     (sha256/sha512/skein/blake3; fletcher and edonr are excluded);
 *   - compression, PSIZE, LSIZE and byteorder match, since blk_cksum is
 *     computed over the physical (post-compression) bytes.
 * Salted checksums (skein/blake3) are safe here because block cloning is
 * always intra-pool (zfs_clone_range_precheck() returns EXDEV otherwise) and
 * the salt is per-pool, so identical data yields identical checksums.
 */
boolean_t
zfs_dedupe_bp_provably_equal(const blkptr_t *bi, const blkptr_t *bo)
{
	enum zio_checksum ck;

	if (zfs_dedupe_bp_same_block(bi, bo))
		return (B_TRUE);

	/*
	 * Two embedded blocks carry their (possibly compressed) payload in
	 * the pointers themselves.  The same payload bytes under the same
	 * compression function decompress to the same data, so equality is
	 * provable without decompressing anything.  A payload mismatch
	 * proves nothing - equal data can compress to different bytes - and
	 * falls through to the read path like any other unprovable case.
	 */
	if (BP_IS_EMBEDDED(bi) && BP_IS_EMBEDDED(bo)) {
		uint8_t pi[BPE_PAYLOAD_SIZE], po[BPE_PAYLOAD_SIZE];

		if (BPE_GET_ETYPE(bi) != BP_EMBEDDED_TYPE_DATA ||
		    BPE_GET_ETYPE(bo) != BP_EMBEDDED_TYPE_DATA ||
		    BP_GET_COMPRESS(bi) != BP_GET_COMPRESS(bo) ||
		    BPE_GET_LSIZE(bi) != BPE_GET_LSIZE(bo) ||
		    BPE_GET_PSIZE(bi) != BPE_GET_PSIZE(bo) ||
		    BP_GET_BYTEORDER(bi) != BP_GET_BYTEORDER(bo))
			return (B_FALSE);

		/*
		 * The psize bit field can encode more than the payload area
		 * holds.  No valid pointer does, but this is cheaper than
		 * trusting that.
		 */
		if (BPE_GET_PSIZE(bi) > BPE_PAYLOAD_SIZE)
			return (B_FALSE);

		decode_embedded_bp_compressed(bi, pi);
		decode_embedded_bp_compressed(bo, po);

		return (memcmp(pi, po, BPE_GET_PSIZE(bi)) == 0);
	}

	if (BP_IS_EMBEDDED(bi) || BP_IS_EMBEDDED(bo) ||
	    BP_IS_HOLE(bi) || BP_IS_HOLE(bo) ||
	    BP_IS_REDACTED(bi) || BP_IS_REDACTED(bo) ||
	    BP_IS_ENCRYPTED(bi) || BP_IS_ENCRYPTED(bo))
		return (B_FALSE);

	ck = BP_GET_CHECKSUM(bi);
	if (ck != BP_GET_CHECKSUM(bo))
		return (B_FALSE);
	if ((zio_checksum_table[ck].ci_flags & ZCHECKSUM_FLAG_DEDUP) == 0)
		return (B_FALSE);

	if (BP_GET_COMPRESS(bi) != BP_GET_COMPRESS(bo) ||
	    BP_GET_PSIZE(bi) != BP_GET_PSIZE(bo) ||
	    BP_GET_LSIZE(bi) != BP_GET_LSIZE(bo) ||
	    BP_GET_BYTEORDER(bi) != BP_GET_BYTEORDER(bo))
		return (B_FALSE);

	return (ZIO_CHECKSUM_EQUAL(bi->blk_cksum, bo->blk_cksum));
}
