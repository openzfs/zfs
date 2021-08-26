#include <sys/zil_pmem_prb.h>
#include <sys/zil_pmem_prb_impl.h>
#include <sys/zfs_pmem.h>
#include <sys/debug.h>
#include <sys/trace_zil_pmem.h>
#include <zfs_fletcher.h>

#include <stdbool.h>

static entry_header_t zero_header = {0};

static int
zilpmem_prb_handle_cmp(const void *va, const void *vb)
{
	const zilpmem_prb_handle_t *a = va;
	const zilpmem_prb_handle_t *b = vb;
	/* we know this function is only used for handles in the same prb */
	VERIFY3P(a->zph_prb, ==, b->zph_prb);

	VERIFY(a->zph_st & ~ZPH_ST_FREED);

	return (TREE_CMP(a->zph_objset_id, b->zph_objset_id));
}

static boolean_t
zilpmem_replay_state_is_init(const zilpmem_replay_state_t *s)
{
	return s->resume_state_active.dtc_gen == 0 && s->resume_state_last.eh_last_gen == UINT64_MAX;
}

void
zilpmem_replay_state_init(zilpmem_replay_state_t *s, uint64_t claim_txg)
{
	memset(s, 0, sizeof(*s));

	s->claim_txg = claim_txg;

	s->resume_state_active.dtc_gen = 0;
	s->resume_state_last.eh_last_gen = UINT64_MAX;
	ASSERT(zilpmem_replay_state_is_init(s));
}

static
void
prb_deptrack_init(prb_deptrack_t *dt)
{
#if PRB_WITH_ASSERT == 1
	dt->dt_dbg_active_prb_write = 0;
#endif
	zilpmem_replay_state_init(&dt->dt_state, 0);
	spl_spin_init(&dt->dt_sl);
}

static void
prb_deptrack_fini(prb_deptrack_t *dt)
{
	spl_spin_destroy(&dt->dt_sl);
}

static void
prb_deptrack_count_minmax_txg(const prb_deptrack_count_t *dtc, uint64_t *min_out,
    uint64_t *max_out)
{
	uint64_t min = UINT64_MAX;
	uint64_t max = 0;
	for (int i = 0; i < TXG_SIZE; i++) {
		max = MAX(max, dtc->dtc_count[i].dtp_txg);
		min = MIN(min, dtc->dtc_count[i].dtp_txg);
	}
	if (min_out != NULL) {
		*min_out = min;
	}
	if (max_out != NULL) {
		*max_out = max;
	}
}

static __always_inline
uint64_t nonzero_u64_from_u32(uint32_t u32_val)
{
	uint32_t lower = u32_val;
	uint32_t upper = (~lower);
	uint64_t nz = (((uint64_t)upper) << 32) | lower;
	ASSERT(nz != 0);
	return nz;
}

static __always_inline
uint64_t nonzero_64bit_checksum_from_crc32(uint32_t crc32)
{
	return nonzero_u64_from_u32(crc32);
}

/* alignment of entry_header_t */
static const size_t ENTRY_HEADER_ALIGN = 256;

static void
prb_committer_init(prb_committer_t *cs)
{
	cs->chunk = NULL;
	cs->staging_header = kmem_alloc_aligned(sizeof(entry_header_t), 256, KM_SLEEP);
	cs->staging_last_256b_block = kmem_alloc_aligned(256, 256, KM_SLEEP);

	return;
}

/*
 * Allocates and initializes the in-DRAM data structure.
 * The memory in [base,base+len) is considered to be PMEM.
 * That PMEM area is not altered by this function.
 *
 * This method does not recover any state from the PMEM area prior to a crash.
 * Callers should use prb_walk_phys() for that purpose.
 */
zilpmem_prb_t *
zilpmem_prb_alloc(size_t ncommitters)
{
	VERIFY(ncommitters > 0);
	VERIFY(ncommitters <= MAX_COMMITTER_SLOTS);

	zilpmem_prb_t *b = kmem_zalloc(sizeof(zilpmem_prb_t), KM_SLEEP);

	b->ncommitters = ncommitters;
	b->committer_slot_distribution.committer_slots = 0;
	spl_sem_init(&b->committer_slot_distribution.committer_sem,
	    ncommitters);

	b->min_chunk_size = UINT64_MAX;
	list_create(&b->all_chunks, sizeof(prb_chunk_t),
	    offsetof(prb_chunk_t, ch_all_list_node));

	list_create(&b->waitclaim_chunks, sizeof(prb_chunk_t),
	    offsetof(prb_chunk_t, ch_current_list_node));
	list_create(&b->free_chunks, sizeof(prb_chunk_t),
	    offsetof(prb_chunk_t, ch_current_list_node));
	list_create(&b->claimed_chunks, sizeof(prb_chunk_t),
	    offsetof(prb_chunk_t, ch_current_list_node));
	for (size_t i = 0; i < TXG_SIZE; i++) {
		list_create(&b->full_chunks[i], sizeof(prb_chunk_t),
		    offsetof(prb_chunk_t, ch_current_list_node));
	}
	avl_create(&b->handles, zilpmem_prb_handle_cmp,
	    sizeof(zilpmem_prb_handle_t),
	    offsetof(zilpmem_prb_handle_t, zph_avl_node));

	b->promised_no_more_gc = 0;

	mutex_init(&b->chunk_mtx, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&b->chunk_cond, NULL, CV_DEFAULT, NULL);

	ASSERT(sizeof(*b->committer) % CACHELINE_LEN == 0);
	b->committer = kmem_alloc_aligned(b->ncommitters * sizeof(*b->committer), CACHELINE_LEN, KM_SLEEP);
	for (size_t i = 0; i < b->ncommitters; i++) {
		prb_committer_init(&b->committer[i]);
	}

	return b;
}

/* XXX this function should take a ref to the ZIL header, not the objset
 * objset should in fact not be part of the on-disk format.
 * The prb_claim function should not receive the ZIL header at all.
 *
 * Fix this together with the introduction of the hash set to prevent
 * log guid collisions.
 */
zilpmem_prb_handle_t *
zilpmem_prb_setup_objset(zilpmem_prb_t *prb, uint64_t objset_id)
{
	mutex_enter(&prb->chunk_mtx);

	zilpmem_prb_handle_t *h = kmem_zalloc(sizeof(*h), KM_SLEEP);
	h->zph_st = ZPH_ST_ALLOCED;
	h->zph_objset_id = objset_id;
	h->zph_prb = prb;

	avl_index_t where;
	if (avl_find(&prb->handles, h, &where) != NULL) {
		panic("objset already set up, maybe forgot to call "
		    "zilpmem_prb_teardown_objset?");
	}
	avl_insert(&prb->handles, h, where);

	mutex_exit(&prb->chunk_mtx);
	return (h);
}

void
zilpmem_prb_promise_no_more_gc(zilpmem_prb_t *prb)
{
	mutex_enter(&prb->chunk_mtx);
	prb->promised_no_more_gc = 1;
	mutex_exit(&prb->chunk_mtx);
}

static boolean_t
zilpmem_prb_have_promised_no_more_gc(zilpmem_prb_t *prb)
{
	VERIFY(MUTEX_HELD(&prb->chunk_mtx));
	return (!!prb->promised_no_more_gc);
}

static void
zilpmem_prb_release_and_free_chunkhold(zilpmem_prb_handle_t *zph);

static void
zilpmem_prb_abandon_claim(zilpmem_prb_handle_t *zph,
    zil_header_pmem_t *out_opaque);

void zilpmem_prb_teardown_objset(zilpmem_prb_handle_t *zph,
    boolean_t abandon_claim, zil_header_pmem_t *out_opaque)
{
	VERIFY3P(zph, !=, NULL);
	 /* if we don't abandon anything there's no update to the ZIL header*/
	EQUIV(abandon_claim, out_opaque != NULL);

	avl_index_t where;
	void *found = avl_find(&zph->zph_prb->handles, zph, &where);
	VERIFY3P(found, ==, zph);
	avl_remove(&zph->zph_prb->handles, zph);

	if (!abandon_claim) {
		mutex_enter(&zph->zph_prb->chunk_mtx);
		VERIFY(zilpmem_prb_have_promised_no_more_gc(zph->zph_prb));
		mutex_exit(&zph->zph_prb->chunk_mtx);
		zilpmem_prb_release_and_free_chunkhold(zph);
	} else {
		VERIFY3P(out_opaque, !=, NULL);
		zilpmem_prb_abandon_claim(zph, out_opaque);
	}
	VERIFY(avl_is_empty(&zph->zph_held_chunks));
	avl_destroy(&zph->zph_held_chunks);

	/* forget about everything (mitigation against use-after-free) */
	memset(zph, 0, sizeof (*zph));
	zph->zph_st = ZPH_ST_FREED;
	kmem_free(zph, sizeof(zilpmem_prb_handle_t));
}

static __always_inline
uintptr_t
chunk_len(const prb_chunk_t *c)
{
	return (((uintptr_t)c->ch_end) - ((uintptr_t)c->ch_base));
}

static void __always_inline
chunk_check_params(const uint8_t *base, size_t chunklen)
{
	VERIFY3P(base, !=, NULL);
	VERIFY3U(chunklen, >, 0);
	VERIFY0(P2PHASE(((uintptr_t)base), sizeof(entry_header_t)));
	VERIFY(ISP2(chunklen));
	VERIFY3U(chunklen, >, sizeof(entry_header_t)); /* non-empty body */
}


prb_chunk_t *
prb_chunk_alloc(uint8_t *pmem_base, size_t len) {
	chunk_check_params(pmem_base, len);
	prb_chunk_t *chunk = kmem_zalloc(sizeof(prb_chunk_t), KM_SLEEP);
	chunk->max_txg = 0;
	chunk->ch_base = pmem_base;
	chunk->ch_cur = chunk->ch_base;
	chunk->ch_end = chunk->ch_base + len;
	zfs_refcount_create(&chunk->ch_rc);
	return (chunk);
}

void prb_chunk_free(prb_chunk_t *c)
{
	ASSERT(zfs_refcount_is_zero(&c->ch_rc));
	zfs_refcount_destroy(&c->ch_rc);
	kmem_free(c, sizeof(*c));
}

static void
chunk_zero_first_256(prb_chunk_t *chunk)
{
	zfs_pmem_memzero256_nt_nodrain(chunk->ch_base, sizeof(entry_header_t));
	zfs_pmem_drain();
}

/* XXX remove this API and replace its users with
 * prb_chunk_initialize_pmem
 * as presented in the thesis
 */
void
zilpmem_prb_add_chunk_for_write(zilpmem_prb_t *prb, prb_chunk_t *chunk)
{
	chunk_zero_first_256(chunk);

	mutex_enter(&prb->chunk_mtx);
	prb->min_chunk_size = MIN(prb->min_chunk_size, chunk_len(chunk));
	list_insert_tail(&prb->free_chunks, chunk);
	list_insert_tail(&prb->all_chunks, chunk);
	cv_broadcast(&prb->chunk_cond);
	mutex_exit(&prb->chunk_mtx);
}

void
zilpmem_prb_add_chunk_for_claim(zilpmem_prb_t *prb, prb_chunk_t *chunk)
{
	// FIXME check if we are still in pre-claim state otherwise panic
	mutex_enter(&prb->chunk_mtx);
	prb->min_chunk_size = MIN(prb->min_chunk_size, chunk_len(chunk));
	list_insert_tail(&prb->waitclaim_chunks, chunk);
	list_insert_tail(&prb->all_chunks, chunk);
	mutex_exit(&prb->chunk_mtx);
}


static void __always_inline
zilpmem_prb_add_chunks_check_params(uint8_t *base, size_t chunklen,
    size_t numchunks)
{
	VERIFY3U(numchunks, >, 0);
	chunk_check_params(base, chunklen);
}

boolean_t
prb_chunk_contains_ptr(const prb_chunk_t *c, const uint8_t *p)
{
	ASSERT3P(p, !=, NULL);
	uintptr_t ubase = (uintptr_t)c->ch_base;
	uintptr_t uend = (uintptr_t)c->ch_end;
	uintptr_t up = (uintptr_t)p;
	return (up >= ubase && up < uend);
}

list_t *
zilpmem_prb_all_chunks(zilpmem_prb_t *prb)
{
	return (&prb->all_chunks);
}

static void
prb_committer_fini(prb_committer_t *cs)
{
	kmem_free(cs->staging_last_256b_block, sizeof(entry_header_t));
	kmem_free(cs->staging_header, 256);

	return;
}

void
zilpmem_prb_free(zilpmem_prb_t *b, boolean_t free_chunks)
{
	mutex_destroy(&b->chunk_mtx);
	spl_sem_destroy(&b->committer_slot_distribution.committer_sem);

	/* destroy committers */
	for (size_t i = 0; i < b->ncommitters; i++) {
		prb_committer_fini(&b->committer[i]);
	}
	kmem_free(b->committer, b->ncommitters * sizeof(*b->committer));

	/* All handles should have been released by now */
	VERIFY(avl_is_empty(&b->handles));
	avl_destroy(&b->handles);

	/* empty the chunk lists that track (but don't own) chunk usage */
	list_t *chunklists[] = {
		&b->waitclaim_chunks, &b->free_chunks, &b->claimed_chunks,
		&b->full_chunks[0], &b->full_chunks[1], &b->full_chunks[2],
		&b->full_chunks[3],
		NULL
	};
	for (list_t **lpp = &chunklists[0]; *lpp != NULL; lpp++) {
		while (list_remove_head(*lpp) != NULL) {};
		ASSERT(list_is_empty(*lpp));
		list_destroy(*lpp);
	}

	/* empty the all_chunks list and free the chunks */
	prb_chunk_t *c;
	while ((c = list_remove_head(&b->all_chunks)) != NULL) {
		ASSERT(!list_link_active(&c->ch_current_list_node));
		if (free_chunks) {
			prb_chunk_free(c);
		} else {
			/* ownership transfer to the caller */
		}
	}
	ASSERT(list_is_empty(&b->all_chunks));
	list_destroy(&b->all_chunks);

	kmem_free(b, sizeof(zilpmem_prb_t));
}

static __always_inline
prb_committer_t*
prb_get_committer_state(zilpmem_prb_t *b, committer_slot_t cs)
{
	return &b->committer[cs.cs_cs];
}


static __always_inline
boolean_t
chunk_has_space(const prb_chunk_t *c, size_t nbytes)
{
	return (c->ch_cur + nbytes <= c->ch_end);
}

static __always_inline
boolean_t
chunk_is_empty(const prb_chunk_t *c)
{
	return (c->ch_cur == c->ch_base);
}

static boolean_t
chunk_is_zeroed_at_current_position(const prb_chunk_t *c)
{
	ASSERT3U(((uintptr_t)c->ch_end), >=, ((uintptr_t)c->ch_cur));
	uintptr_t rlen = c->ch_end - c->ch_cur;
	uintptr_t checklen = MIN(rlen, sizeof (entry_header_t));
	/*
	 * We always write in 256b multiples
	 * => either there is no space or 256 bytes
	 */
	CTASSERT(sizeof (entry_header_t) == 256);
	ASSERT(checklen == 256 || checklen == 0);

	entry_header_t tmp __attribute__ ((__aligned__ (512)));
	/* "load" from pmem */
	memcpy(&tmp, c->ch_cur, checklen);
	return (memcmp(&tmp, &zero_header, checklen) == 0);
}


static prb_chunk_t *
get_chunk(zilpmem_prb_t *b, boolean_t sleep, prb_write_stats_t *stats)
{
	stats->get_chunk_calls++;
	prb_chunk_t *c = NULL;
	mutex_enter(&b->chunk_mtx);
	while (1) {
		c = list_remove_head(&b->free_chunks);
		if (likely(c != NULL)) {
			break;
		}
		stats->get_chunk_calls_sleeps++;
		if (sleep) {
			cv_wait(&b->chunk_cond, &b->chunk_mtx);
			continue;
		} else {
			break;
		}
	}
	if (c != NULL) {
		ASSERT3U(c->max_txg, ==, 0);
		ASSERT(c->ch_cur == c->ch_base);
	} else {
		ASSERT(!sleep);
	}
	mutex_exit(&b->chunk_mtx);
	return (c);
}

static void
entry_body_fletcher4(const void *body_dram, size_t body_len, zio_cksum_t *out)
{
	if (IS_P2ALIGNED(body_len, sizeof (uint32_t))) {
		fletcher_4_native(body_dram, body_len, NULL, out);
	} else {
		fletcher_4_native_varsize(body_dram, body_len, out);
	}
}


prb_write_raw_chunk_result_t prb_write_chunk(prb_chunk_t *entry_chunk, uint64_t objset_id,
    uint64_t zil_guid_1,
    uint64_t zil_guid_2, uint64_t txg, uint64_t gen, uint64_t gen_scoped_id,
	eh_dep_t dep, const uint8_t *body_dram, size_t body_len,
	entry_header_t *staging_header,
	uint8_t *staging_last_256b_block,
	prb_write_stats_t *stats_out)
{
	ASSERT(entry_chunk);
	ASSERT(body_dram != NULL);

	/* NB: non-zeroness is part of on-disk format */
	VERIFY3U(body_len, >, 0);
	VERIFY3U(txg, !=, 0);
	VERIFY3U(gen, !=, 0);
	VERIFY3U(gen_scoped_id, !=, 0);
	VERIFY3U(zil_guid_1, !=, 0);
	VERIFY3U(zil_guid_2, !=, 0);
	VERIFY3U(objset_id, !=, 0);

	const size_t body_resid_len = body_len % 256;
	const size_t body_bulk_len = body_len - body_resid_len;
	ASSERT0(body_bulk_len % 256);
	const size_t entry_space_without_resid_pad = sizeof(entry_header_t) + body_bulk_len + body_resid_len;
	CTASSERT(P2ROUNDUP_TYPED(23, 32, int) == 32);
	CTASSERT(P2ROUNDUP_TYPED(5, 2, int) == 6);
	const size_t entry_space = P2ROUNDUP_TYPED(entry_space_without_resid_pad, 256, size_t);
	const size_t resid_pad = entry_space - entry_space_without_resid_pad;
	ASSERT0(entry_space % 256);
	ASSERT(entry_space >= 512);

	if (!chunk_has_space(entry_chunk, entry_space)) {
		return WRITE_CHUNK_ENOSPACE;
	}

	/*
	 * Prepare the header in DRAM + compute checksums
	 * TODO: Encryption.
	 * XXX see notes from 2021-01-29: the case for per-cpu staging buffers
	 *     for how we can avoid holding a commit slot while encrypting /
	 *     checksumming
	 */
	entry_header_t *header = staging_header;
	bzero(header, sizeof(*header));
	header->eh_data.eh_objset_id = objset_id;
	header->eh_data.eh_zil_guid_1 = zil_guid_1;
	header->eh_data.eh_zil_guid_2 = zil_guid_2;
	header->eh_data.eh_txg = txg;
	header->eh_data.eh_gen = gen;
	header->eh_data.eh_gen_scoped_id = gen_scoped_id;
	CTASSERT(sizeof(dep) == sizeof(header->eh_data.eh_dep));
	memcpy(&header->eh_data.eh_dep, &dep, sizeof(dep));
	header->eh_data.eh_len = body_len;
	/*
	 * Header + body checksums are done below so that we only have to
	 * save & restore kfpu context once and keep the section as sort
	 * as possible.
	 */

	/*
	 * Prepare the last 256b chunk of the insert in DRAM
	 */
	ASSERT(body_resid_len + resid_pad == 0 ||
	    body_resid_len + resid_pad == 256);
	uint8_t *staging_start = staging_last_256b_block;
	bzero(staging_start, 256);
	uint8_t *staging_cur = staging_start;
	bcopy(body_dram + body_bulk_len, staging_cur, body_resid_len);
	staging_cur += body_resid_len;
	staging_cur += resid_pad; /* we already bzeroed the staging area */
	size_t staging_len = (uintptr_t)staging_cur - (uintptr_t)staging_start;

	ASSERT(!list_link_active(&entry_chunk->ch_current_list_node));

	/* BEGIN MODIFY PMEM:
	 *
	 *    1 - Zero-out follow header space.
	 *        Write out body.
	 *
	 *    SFENCE
	 *
	 *    2 - Write out header.
	 *
	 *    SFENCE
	 *
	 * Crash Consistency:
	 *
	 * If we crash in (1), the header space which we would write to in (2)
	 * is guaranteed to be zero courtesy of a prior invocation of this
	 * function. Thus the potentially incompletely written body is not
	 * reachable for traversal.
	 *
	 * If we crash in (2), the header space is
	 * a) still not modified => same case as (1),
	 * b) fully written out => as if we had returned,
	 * c) partially written, which is what the remainder
	 *    of this block comment is concerned with:
	 *
	 * 1. Before we started writing the header, it was guaranteed to be
	 *    zeroed out.
	 * 2. All fields in eh_data are guaranteed to be non-zero when they
	 *    have been fully written out.
	 * 3. Stores to 8 byte sized + aligned chunks are powerfail atomic.
	 * 4. eh_data and all fields within it are 8 byte sized + aligned.
	 *
	 * Thus, before we start writing the header, both header and body have
	 * well-defined contents. The zeroed-out header will make traversal
	 * skip the entry and remainder of the chunk.
	 *
	 * If we crash with a partially written header, (3) and (4) guarantee
	 * that the fields are not 'torn', i.e., they will have either the
	 * intended non-zero value or be zero.
	 * Traversal will skip an entry and the remaining part of its chunk
	 * if it encounters such a header with one or more zero eh_data fields.
	 *
	 * Note that, in the absence of bit errors, this "zero/non-zero scheme"
	 * means that we do not have to rely on the checksum for crash
	 * consistency.
	 *
	 * Bit Errors:
	 *
	 * The reliance on the non-zero scheme to detect partially written
	 * headers is brittle in the presence of bit errors. Traversal
	 * implements the following mitigations to detect a corrupted header
	 * (and body):
	 *
	 * - Additional checks for fields in eh_data:
	 *   - TODO we could replicate eh_len bits since we'd be fine with 32bit
	 *   - ???
	 * - FLETCHER4 checksum over the body.
	 * - FLETCHER4 checksum over the header, including the body checksum.
	 *
	 * We use a separate FLETCHER4 for the header because
	 * a) it is small and fixed-size which improves error detection
	 *    capability
	 * b) we have the space since for performance reasons, the header is
	 *     padded to 256 bytes.
	 *
	 * When prb_chunk_iter() detects corrupted entry data, it skips the
	 * entry and the remainder of the entry's chunk.
	 * Corrupted body data is handled in
	 * zilpmem_prb_replay_read_replay_node().
	 */

	/*
	 * XXX we should prepare the way for error handling of PMEM writes
	 *     in the following section.
	 * - release chunks if pmem write fails?
	 * - undo chunk space accounting? (we haven't done any so far)
	 */


	uint8_t *header_pmem = entry_chunk->ch_cur;
	CTASSERT(_Alignof(entry_header_t) <= 8);
	ASSERT3U(((uintptr_t)header_pmem) % 8, ==, 0);
	ASSERT3U(((uintptr_t)header_pmem) % 256, ==, 0);

	/*
	 * Assert that the space that we write the header to is zeroed out
	 * so all the crash-consistency considerations outlined above hold.
	 */
	if (PRB_WITH_ASSERT) {
		entry_header_t tmp;
		ASSERT(chunk_has_space(entry_chunk, sizeof (tmp)));
		/* "load" from pmem */
		memcpy(&tmp, header_pmem, sizeof (tmp)); // XXX zfs_pmem_memcpy_mcsafe
		ASSERT0(memcmp(&tmp, &zero_header, sizeof (tmp)));
	}

	/* Compute the checksums */
	/* FIXME handle zero value */
	entry_body_fletcher4(body_dram, body_len, &header->eh_data.eh_body_csum);

	/*
	 * Put result on stack so that the checksumming function checksums
	 * header->eh_data.eh_header_csum == {0}
	 */
	/* FIXME handle zero value */
	ASSERT(ZIO_CHECKSUM_IS_ZERO(&header->eh_data.eh_header_csum));
	zio_cksum_t header_csum;
	fletcher_4_native(header, sizeof(*header), NULL, &header_csum);
	header->eh_data.eh_header_csum = header_csum;

	/* zero the follow header space in this chunk */
	uint8_t *entry_chunk_next_cur = entry_chunk->ch_cur + entry_space;
	ASSERT3P(entry_chunk_next_cur, <=, entry_chunk->ch_end);
	uintptr_t rlen = MIN(256, entry_chunk->ch_end - entry_chunk_next_cur);
	ASSERT3U((rlen % 256), ==, 0); /* that's our granularity anyways */
	zfs_pmem_memzero256_nt_nodrain(entry_chunk_next_cur, rlen);


	/* write out bulk of the body */
	ASSERT3U(((uintptr_t)header_pmem + sizeof(entry_header_t)) % 256, ==,
	    0);
	ASSERT3U(body_bulk_len % 256, ==, 0);
	zfs_pmem_memcpy256_nt_nodrain(
	    header_pmem + sizeof(entry_header_t),
	    body_dram,
	    body_bulk_len);
	/* write out trailing part of body */
	ASSERT3U(staging_len % 256, ==, 0);
	zfs_pmem_memcpy256_nt_nodrain(
	    header_pmem + sizeof(entry_header_t) + body_bulk_len,
	    staging_start,
	    staging_len);

	/* end of phase (1) */
	zfs_pmem_drain();

	/*
	 * Experiments show that it is cheapest to write out the entire 256B,
	 * including the zero padding which we know is _already_ zero at
	 */
	ASSERT((uintptr_t)header_pmem % 256 == 0);
	zfs_pmem_memcpy256_nt_nodrain(header_pmem, header,
	    sizeof(entry_header_t));

	/* end of phase (2) */
	zfs_pmem_drain();

	/* END MODIFY PMEM */

	/* chunk accounting */
	entry_chunk->ch_cur += entry_space;
	entry_chunk->max_txg = MAX(entry_chunk->max_txg, txg);

	/* stats */
	stats_out->entry_chunk = entry_chunk;
	stats_out->entry_pmem_base = header_pmem;

	return WRITE_CHUNK_OK;
}


static inline
uint64_t
timedelta_nanos(hrtime_t *since)
{
	hrtime_t now = gethrtime();
	hrtime_t delta = now - *since;
	*since = now;
	return (delta);
}

static committer_slot_t
prb_zil_get_committer_slot(zilpmem_prb_t *b)
{

	// TODO: prevent migration of this thread to other cpu until
	// prb_zil_put_committer_slot is called.
	// (Is that actually a good idea?)

	/*
	 * Ensure that there is _some_ slot, wait otherwise.
	 */
	spl_sem_wait(&b->committer_slot_distribution.committer_sem);

	/*
	 * Find ourselves a slot.
	 *
	 * Note that there is no strict upper bound, starvation prevention or
	 * similar for a thread that reaches this point.
	 * If a thread A spends X seconds between prb_zil_{get,put}_commiter_slot
	 * and we (thread B) are so unlucky that we spend >X seconds trying to find
	 * a slot (e.g. because we are preempted), then after A left the semaphore,
	 * another thread C might win the semaphore and aquire A's former slot
	 * instead of us.
	 * But we suspect that this condition is very rare, so it's better to
	 * gamble on X being much larger than the time we spent in this function.
	 */
	uint64_t committer_slots;
	const uint64_t ncommitters_mask = (1ULL << b->ncommitters) - 1;
	committer_slots = __atomic_load_n(
	    &b->committer_slot_distribution.committer_slots, __ATOMIC_SEQ_CST);
	while (true) {
		ASSERT((committer_slots & (~ncommitters_mask)) == 0);
		int idx_plus_1 = __builtin_ffs(~committer_slots);
		ASSERT(idx_plus_1 > 0); // semaphore ensures that there is a slot left
		int idx = idx_plus_1 - 1;
		ASSERT(idx < MAX_COMMITTER_SLOTS);
		ASSERT(((1ULL << idx) & committer_slots) == 0); /* builtin_ffs works */
		const uint64_t slot_mask = (1ULL << idx);
		bool won = __atomic_compare_exchange_n(
			&b->committer_slot_distribution.committer_slots,
			&committer_slots,
			committer_slots | slot_mask,
			 false, /* no weak ordering */
			__ATOMIC_SEQ_CST, /* TODO didn't think too hard here */
			__ATOMIC_SEQ_CST /* TODO didn't think too hard here */
		);
		if (won) {
			committer_slot_t cs = {.cs_cs = idx};
			return cs;
		} else {
			// committer_slots contains updated b->committer_slots
		}
	}
}

static void
prb_zil_put_committer_slot(zilpmem_prb_t *b, committer_slot_t s)
{
	const uint64_t slot_mask = 1ULL << s.cs_cs;
	uint64_t committer_slots = __atomic_fetch_and(
		&b->committer_slot_distribution.committer_slots,
	    ~slot_mask, __ATOMIC_SEQ_CST);
	ASSERT((committer_slots & slot_mask) != 0);
	spl_sem_post(&b->committer_slot_distribution.committer_sem);
}

typedef enum deptrack_outcome {
	DEPTRACK_OUTCOME_SAME_GEN,
	DEPTRACK_OUTCOME_TXG_SHOULD_HAVE_SYNCED_ALREADY,
	DEPTRACK_OUTCOME_BEGAN_NEW_GEN,
	DEPTRACK_OUTCOME_ACTIVE_HAS_NEWER_GEN,
	DEPTRACK_OUTCOME_ACTIVE_HAS_NEWER_ID,
} deptrack_outcome_t;

static void
zilpmem_do_deptrack_compute_eh_dep_t_from_active(const prb_deptrack_count_t *active, eh_dep_t *last)
{
	/*
	* Compute dt_eh_dep from `active`.
	*
	* Use the TXG_CONCURRENT_STATES most recent counters of the last
	* generation, i.e.
	* 	max := max txg in `last`
	*	counters with dtp_txg in {max, max-1, max-2}
	* Older counters are automatically obsolete because their txg has
	* already synced out so replay will ignore them anyways.
	*/
	memset(last, 0, sizeof(*last));
	last->eh_last_gen = active->dtc_gen;
	uint64_t min, max;
	prb_deptrack_count_minmax_txg(active, &min, &max);
	for (size_t i = 0; i < TXG_CONCURRENT_STATES; i++) {
		if (i >= max) { // FIXME this is incorrect, need test
			continue;
		}
		uint64_t t = max - i;
		const prb_deptrack_count_pair_t *p = &active->dtc_count[t & TXG_MASK];
		if (p->dtp_txg == t) {
			last->eh_last_gen_counts[i] = *p;
		}
	}
}

static deptrack_outcome_t
zilpmem_do_deptrack(prb_deptrack_count_t *active, eh_dep_t *last, uint64_t txg, uint64_t gen, uint64_t id)
{
	/* FIXME turn these into errors */
	VERIFY3U(txg, >, 0);
	VERIFY3U(gen, >, 0);
	VERIFY3U(id, >, 0);

	uint64_t max_txg;
	prb_deptrack_count_minmax_txg(active, NULL, &max_txg);
	if (max_txg >= TXG_CONCURRENT_STATES &&
	    txg <= max_txg - TXG_CONCURRENT_STATES)
		return DEPTRACK_OUTCOME_TXG_SHOULD_HAVE_SYNCED_ALREADY;

	if (gen < active->dtc_gen)
		return DEPTRACK_OUTCOME_ACTIVE_HAS_NEWER_GEN;

	bool beginning_new_gen = gen > active->dtc_gen;

	if (!beginning_new_gen && id <= active->dtc_last_id)
		return DEPTRACK_OUTCOME_ACTIVE_HAS_NEWER_ID;

	/* we only modify active and last from this point on */

	deptrack_outcome_t ret;
	if (beginning_new_gen) {
		ret = DEPTRACK_OUTCOME_BEGAN_NEW_GEN;
		zilpmem_do_deptrack_compute_eh_dep_t_from_active(active, last);
		/*
		* Update `active->dtc_gen`
		*/
		active->dtc_gen = gen;
		active->dtc_last_id = 0;
	} else {
		ret = DEPTRACK_OUTCOME_SAME_GEN;
	}
	ASSERT3U(active->dtc_gen, >, last->eh_last_gen);
	ASSERT3U(id, >, active->dtc_last_id);
	active->dtc_last_id = id;

	/* update `active->dtc_count` */
	bool new_txg = active->dtc_count[txg & TXG_MASK].dtp_txg != txg;
	if (new_txg) {
		/*
		* This assertion holds because we already covered the
		* 'obsolete' case above
		*/
		ASSERT(active->dtc_count[txg & TXG_MASK].dtp_txg < txg);
		active->dtc_count[txg & TXG_MASK].dtp_txg = txg;
		active->dtc_count[txg & TXG_MASK].dtp_count = 0;
	}
	/* invariant produced by the `if` directly above */
	ASSERT(active->dtc_count[txg & TXG_MASK].dtp_txg == txg);
	active->dtc_count[txg & TXG_MASK].dtp_count++;
	return ret;
}


static prb_write_result_t prb_write(
    zilpmem_prb_t *b,
    prb_deptrack_t *dt,
    uint64_t objset,
    uint64_t zil_guid_1,
    uint64_t zil_guid_2,
    uint64_t txg,
    boolean_t needs_new_gen,
    size_t body_len,
    const void *body_dram,
    boolean_t may_sleep,
    prb_write_stats_t *stats_out)
{
	ASSERT(b != NULL);
	ASSERT(dt != NULL);
	ASSERT(body_dram != NULL);
	/* stats_out is allowed to be 0 */

	prb_write_stats_t stats = {0};
	hrtime_t td;

	(void) timedelta_nanos(&td); /* start measuring */

	/*
	 * Get a committer slot.
	 */
	committer_slot_t cslot = prb_zil_get_committer_slot(b);
	stats.get_committer_slot_nanos = timedelta_nanos(&td);
	stats.committer_slot = cslot.cs_cs;

	spl_spin_lock(&dt->dt_sl);
	stats.dt_sl_aquisition_nanos = timedelta_nanos(&td);

	zilpmem_replay_state_t *st = &dt->dt_state;
	prb_deptrack_count_t *active = &st->resume_state_active;
	eh_dep_t *last = &st->resume_state_last;
	uint64_t gen = active->dtc_gen + (needs_new_gen ? 1 : 0);
	if (needs_new_gen) {
		/*
		 * Crash if we every reach the wraparound state. We need to
		 * crash because replay uses `gen` as a sort key.
		 * If we write an entry every nano second (which is 1000x faster
		 * than currently available PMEM hardware) we'd have 584 years
		 * until this situation occurs.
		 */
		VERIFY3U(gen, >, active->dtc_gen);
	} else {
		ASSERT3U(gen, ==, active->dtc_gen);
	}
	uint64_t gen_scoped_id = active->dtc_gen == gen ? active->dtc_last_id + 1 : 1;
	deptrack_outcome_t dtoutcome = zilpmem_do_deptrack(active, last, txg, gen, gen_scoped_id);
	switch (dtoutcome) {
		case DEPTRACK_OUTCOME_SAME_GEN:
#if PRB_WITH_ASSERT == 1
			dt->dt_dbg_active_prb_write++;
#endif
			break;
		case DEPTRACK_OUTCOME_TXG_SHOULD_HAVE_SYNCED_ALREADY:
			/* ASSERT last_synced_txg >= txg; */
			stats.obsolete++;
			break; /* we exit early after releasing the spinlock below */
		case DEPTRACK_OUTCOME_BEGAN_NEW_GEN:
			stats.beginning_new_gen++;
			/*
			 * - Assert that the caller took care of serializing generations
			 *   before adding ourselves to the counter.
			 */
#if PRB_WITH_ASSERT == 1
			ASSERT0(dt->dt_dbg_active_prb_write);
			dt->dt_dbg_active_prb_write++;
#endif
			break;
		case DEPTRACK_OUTCOME_ACTIVE_HAS_NEWER_GEN:
			panic("caller must assert that generation numbers are monotonic. active->dtc_gen=%llu gen=%llu", active->dtc_gen, gen);
		case DEPTRACK_OUTCOME_ACTIVE_HAS_NEWER_ID:
			panic("deptrack doesn't use dtc_last_id as we expect it to");
	}

	spl_spin_unlock(&dt->dt_sl);
	stats.dt_sl_held_nanos = timedelta_nanos(&td);

	if (dtoutcome == DEPTRACK_OUTCOME_TXG_SHOULD_HAVE_SYNCED_ALREADY) {
		prb_zil_put_committer_slot(b, cslot);
		return PRB_WRITE_OBSOLETE;
	}

	/* write to PMEM without spinlock held */

	/*
	 * We are going to carve out the allocation from this committers's current
	 * chunk or, if the space left in the committers's current chunk is too
	 * small, grab the global prb lock and get a new chunk.
	 */
	prb_committer_t *cs = prb_get_committer_state(b, cslot);

	boolean_t fresh = B_FALSE;
	if (cs->chunk == NULL) {
		cs->chunk = get_chunk(b, may_sleep, &stats);
		fresh = B_TRUE;
	}
	prb_write_result_t ret;
	if (cs->chunk == NULL) {
		ASSERT(!may_sleep);
		ret = PRB_WRITE_EWOULDSLEEP;
	} else {
		ASSERT(cs->chunk);
		prb_write_raw_chunk_result_t wr_chunk_res;
retry_with_fresh_chunk:
		wr_chunk_res = prb_write_chunk(cs->chunk, objset, zil_guid_1, zil_guid_2,
			txg, gen, gen_scoped_id, *last,
			body_dram, body_len, cs->staging_header, cs->staging_last_256b_block,
			&stats);
		switch (wr_chunk_res) {
		case WRITE_CHUNK_OK:
			ret = PRB_WRITE_OK;
			break;
		case WRITE_CHUNK_ENOSPACE:
			if (!fresh) {
				/* non-fresh chunks might have insufficient capacity => allow them a new chunk */
				ASSERT(cs->chunk);

				/*
				 * If we are moving to a new chunk, ensure that the chunk we are leaving has
				 * been zeroed out at its current position. This validates that
				 * we zero out follow headers in the chunk we are writing to.
				 */
				ASSERT(chunk_is_zeroed_at_current_position(cs->chunk));

				/* Move the full chunk to the full list so that it can be gc'ed */
				/* FIXME turn into function */
				mutex_enter(&b->chunk_mtx);
				list_t *l = &b->full_chunks[
				cs->chunk->max_txg & TXG_MASK];
				list_insert_head(l, cs->chunk);
				mutex_exit(&b->chunk_mtx);
				cs->chunk = NULL;

				/* Get a new chunk */
				cs->chunk = get_chunk(b, may_sleep, &stats);
				if (cs->chunk == NULL) {
					ASSERT(!may_sleep);
					ret = PRB_WRITE_EWOULDSLEEP;
					break;
				}
				ASSERT(cs->chunk);
				fresh = B_TRUE;
				goto retry_with_fresh_chunk;
			} else {
				ASSERT3U(chunk_len(cs->chunk), >=, b->min_chunk_size);
				panic("caller must not request allocations larger than the smallest chunk: %llu", b->min_chunk_size);
			}
			break;
		default:
			panic("unexpected result: %d", wr_chunk_res);
		}
		ASSERT3S(ret, ==, PRB_WRITE_OK); // FIXME probably incorrect for PRB_WRITE_EWOULDSLEEP
	}

	/* FIXME rollback gen_scoped_id if write fails => restructure
	 * code to get the chunk before the gen_scoped_id
	 */
	switch (ret) {
	case PRB_WRITE_OK:
		break;
	case PRB_WRITE_EWOULDSLEEP:
		break;
	case PRB_WRITE_OBSOLETE:
		panic("PRB_WRITE_OBSOLETE unexpeced here: %d", ret);
	}

	/*
	 * Account all time spent on pmem access to the right *_nanos value.
	 * TODO: conditional compilation once we make time profiling conditional.
	 */
	asm volatile ("mfence;" ::: "memory");

	stats.pmem_nanos = timedelta_nanos(&td);

#if PRB_WITH_ASSERT == 1
	spl_spin_lock(&dt->dt_sl);
	stats.dt_sl_aquisition_nanos += timedelta_nanos(&td);
	ASSERT(dt->dt_dbg_active_prb_write > 0);
	dt->dt_dbg_active_prb_write--;
	spl_spin_unlock(&dt->dt_sl);
	stats.dt_sl_held_nanos += timedelta_nanos(&td);
#endif

	prb_zil_put_committer_slot(b, cslot);
	stats.put_committer_slot_nanos = timedelta_nanos(&td);

	DTRACE_PROBE2(zil_pmem_prb_write_entry__done, zilpmem_prb_t *, b, prb_write_stats_t *, &stats);

	if (stats_out != NULL) {
		*stats_out = stats;
	}

	return ret;
}

static void
zilpmem_prb_gc_impl_chunk(prb_chunk_t *chunk)
{
	/* zero out chunk's first heeader so that we overwrite zeroes */
	chunk_zero_first_256(chunk);

	/* reset for use */
	chunk->max_txg = 0;
	chunk->ch_cur = chunk->ch_base;
}

/*
 * Free up all the chunks in the full_list that have a max_txg >= txg.
 * Free up all waitclaim chunks that are no longer referenced.
 */
void zilpmem_prb_gc(zilpmem_prb_t *b, uint64_t txg)
{
	mutex_enter(&b->chunk_mtx);

	VERIFY(!zilpmem_prb_have_promised_no_more_gc(b));

	list_t *l = &b->full_chunks[txg & TXG_MASK];

	boolean_t freed = B_FALSE;
	prb_chunk_t *chunk;
	while ((chunk = list_remove_head(l)) != NULL) {
		ASSERT3U((chunk->max_txg & TXG_MASK), ==, (txg & TXG_MASK));
		ASSERT3U(chunk->max_txg, <=, txg);

		/* TODO stats about chunk utilization go here */

		zilpmem_prb_gc_impl_chunk(chunk);

		list_insert_head(&b->free_chunks, chunk);
		freed = B_TRUE;
	}
	ASSERT(list_is_empty(l));

	l = &b->waitclaim_chunks;
	prb_chunk_t *chunk_next;
	for (chunk = list_head(l); chunk != NULL; chunk = chunk_next) {
		chunk_next = list_next(l, chunk);
		if (!zfs_refcount_is_zero(&chunk->ch_rc)) {
			continue;
		}

		list_remove(l, chunk);
		zilpmem_prb_gc_impl_chunk(chunk);
		list_insert_tail(&b->free_chunks, chunk);
		freed = B_TRUE;
	}

	if (freed) {
		cv_broadcast(&b->chunk_cond);
	}

	mutex_exit(&b->chunk_mtx);
}

void prb_chunk_iter_init(const uint8_t *base_pmem, size_t len,
    prb_chunk_iter_t *w)
{
	chunk_check_params(base_pmem, len);
	w->cur = base_pmem;
	w->end = base_pmem + len;
}

static prb_chunk_iter_result_t
prb_chunk_iter_provided_eh_buf(prb_chunk_iter_t *w, uint8_t const **out,
    entry_header_t *header_buf)
{
	int err;

	if (w->cur >= w->end) {
		*out = NULL;
		bzero(header_buf, sizeof(*header_buf));
		return PRB_CHUNK_ITER_OK;
	}

	const uint8_t *entry = w->cur;

	ASSERT((uintptr_t)entry % ENTRY_HEADER_ALIGN == 0);
	const entry_header_t *entry_header_pmem =
		(const entry_header_t *) entry;

	err = zfs_pmem_memcpy_mcsafe(header_buf, entry_header_pmem,
	    sizeof (entry_header_t));
	if (err != 0)
		return PRB_CHUNK_ITER_ERR_MCE;

	const zio_cksum_t tmp = header_buf->eh_data.eh_header_csum;
	ZIO_SET_CHECKSUM(&header_buf->eh_data.eh_header_csum,
		0, 0, 0, 0);
	zio_cksum_t header_csum;
	fletcher_4_native(header_buf, sizeof(*header_buf), NULL, &header_csum);
	header_buf->eh_data.eh_header_csum = tmp; /* restore */

	if (!ZIO_CHECKSUM_EQUAL(header_buf->eh_data.eh_header_csum,
		header_csum))
		return PRB_CHUNK_ITER_ERR_HDR_CHECSUM;

	/* XXX sanity check on max length would be nice */

	if (header_buf->eh_data.eh_zil_guid_1 == 0 ||
	    header_buf->eh_data.eh_zil_guid_2 == 0) {
		/* XXX this needs testing & test coverage */
		return PRB_CHUNK_ITER_ERR_INVALID_LOG_GUID;
	}

	if (header_buf->eh_data.eh_len == 0)
		return PRB_CHUNK_ITER_ERR_INVALID_LEN;

	const void *body = entry + sizeof(entry_header_t);
	size_t body_len = header_buf->eh_data.eh_len;

	if ((uintptr_t)(body + body_len) >= ((uintptr_t)w->end)) {
		/* XXX this needs testing & test coverage */
		return PRB_CHUNK_ITER_ERR_BODY_OUT_OF_BOUNDS;
	}

	/*
	 * Body checksum validation is done in
	 * zilpmem_prb_replay_read_replay_node() when the entry is actually
	 * read.
	 */

	*out = w->cur;

	w->cur = (uint8_t*)P2ROUNDUP_TYPED(
		body + body_len, ENTRY_HEADER_ALIGN, uintptr_t
	);

	return PRB_CHUNK_ITER_OK;
}

prb_chunk_iter_result_t
prb_chunk_iter(prb_chunk_iter_t *w, uint8_t const **out)
{
	entry_header_t hdr;
	return prb_chunk_iter_provided_eh_buf(w, out, &hdr);
}


prb_write_result_t zilpmem_prb_write_entry_with_stats(
	zilpmem_prb_handle_t *zph,
	uint64_t txg,
	boolean_t needs_new_gen,
	size_t body_len,
	const void *body_dram,
	boolean_t may_sleep,
	prb_write_stats_t *stats_out)
{

	// FIXME concurrency
	if (zph->zph_st & ~(ZPH_ST_LOGGING)) {
		panic("unexpected state %d", zph->zph_st);
	}

	prb_write_stats_t stats;
	prb_write_result_t res;
	res = prb_write(
	    zph->zph_prb,
	    &zph->zph_deptrack,
	    zph->zph_objset_id,
	    zph->zph_zil_guid_1,
	    zph->zph_zil_guid_2,
	    txg,
	    needs_new_gen,
	    body_len, body_dram, may_sleep, &stats);

	if (stats_out)
		*stats_out = stats;

#ifdef KERNEL
	if (res != PRB_WRITE_OK)
		pr_debug("prb_write returned %d\n", res);
#endif

    return res != PRB_WRITE_OK;
}

int zilpmem_prb_write_entry(
	zilpmem_prb_handle_t *zph,
	uint64_t txg,
	boolean_t needs_new_gen,
	size_t body_len,
	const void *body_dram)
{
	return (zilpmem_prb_write_entry_with_stats(zph, txg, needs_new_gen,
	    body_len, body_dram, B_TRUE, NULL));
}

boolean_t
zil_header_pmem_state_valid(uint64_t st)
{
	switch (st) {
		case ZHPM_ST_NOZIL:
			/* fallthrough */
		case ZHPM_ST_REPLAYING:
			/* fallthrough */
		case ZHPM_ST_LOGGING:
			return B_TRUE;
		default:
			return B_FALSE;
	}
}

boolean_t
zil_header_pmem_validate_format(const zil_header_pmem_t *zho)
{
	ASSERT(zho);
	const zil_header_pmem_impl_t *zh = (const zil_header_pmem_impl_t *)zho;
	return (zil_header_pmem_state_valid(zh->zhpm_st));
	/* TODO more validation */
}

void
zil_header_pmem_init(zil_header_pmem_t *zho)
{
	zil_header_pmem_impl_t *zh = (zil_header_pmem_impl_t *)zho;
	memset(zh, 0, sizeof(*zh));
	zh->zhpm_st = ZHPM_ST_NOZIL;
	ASSERT(zil_header_pmem_validate_format(zho));
}

void
zil_header_pmem_state_from_header(const zil_header_pmem_t *zho,
    zil_header_pmem_state_t *out, boolean_t *valid)
{
	const zil_header_pmem_impl_t *zh = (const zil_header_pmem_impl_t *)zho;
	*valid = zil_header_pmem_state_valid(zh->zhpm_st);
	if (*valid) {
		*out = zh->zhpm_st;
	} else {
		ASSERT(!zil_header_pmem_state_valid(-1));
		*out = -1;
	}
}

void
zil_header_pmem_claimtxg_from_header(const zil_header_pmem_t *zho,
    uint64_t *claim_txg, boolean_t *valid)
{
	const zil_header_pmem_impl_t *zh = (const zil_header_pmem_impl_t *)zho;
	if (zh->zhpm_st != ZHPM_ST_REPLAYING) {
		*valid = B_FALSE;
		*claim_txg = UINT64_MAX;
	} else {
		*valid = B_TRUE;
		*claim_txg = zh->zhpm_replay_state.claim_txg;
	}
}

const char *zil_header_pmem_state_debug_str(zil_header_pmem_state_t s)
{
	switch (s) {
	case ZHPM_ST_NOZIL: return "nozil";
	case ZHPM_ST_LOGGING: return "logging";
	case ZHPM_ST_REPLAYING: return "replaying";
	default:
		return NULL;
	}
}

char *prb_deptrack_count_pair_debug_string(const prb_deptrack_count_pair_t *p)
{
	return (kmem_asprintf("(%llu,%llu)", p->dtp_txg, p->dtp_count));
}

char *eh_dep_t_debug_string(const eh_dep_t *eh)
{
	VERIFY3U(sizeof(eh->eh_last_gen_counts)/sizeof(eh->eh_last_gen_counts[0]), ==, 3);
	char *a = prb_deptrack_count_pair_debug_string(&eh->eh_last_gen_counts[0]);
	char *b = prb_deptrack_count_pair_debug_string(&eh->eh_last_gen_counts[1]);
	char *c = prb_deptrack_count_pair_debug_string(&eh->eh_last_gen_counts[2]);
	char *ret = kmem_asprintf("(%llu,{%s,%s,%s})",
	    eh->eh_last_gen, a, b, c);
	kmem_strfree(a);
	kmem_strfree(b);
	kmem_strfree(c);
	return (ret);
}

char *
zilpmem_replay_state_phys_debug_string( const zilpmem_replay_state_phys_t *s)
{
	char *a = eh_dep_t_debug_string(&s->resume_state_active);
	char *l = eh_dep_t_debug_string(&s->resume_state_last);
	char *ret = kmem_asprintf("{claim_txg=%llu, active=%s, last=%s}",
	    s->claim_txg, a, l);
	kmem_strfree(a);
	kmem_strfree(l);
	return (ret);
}

char *
zil_header_pmem_debug_string(const zil_header_pmem_t *zh_opaque)
{
	const zil_header_pmem_impl_t *zh =
	    (const zil_header_pmem_impl_t*)zh_opaque;
	CTASSERT(sizeof(*zh) == sizeof(*zh_opaque));
	char *rst_string = zilpmem_replay_state_phys_debug_string(&zh->zhpm_replay_state);
	const char *st_str = zil_header_pmem_state_debug_str(zh->zhpm_st);
	if (st_str == NULL)
		st_str = "invalid";

	char *st_string =  kmem_asprintf("%s(0x%x)", st_str, zh->zhpm_st);
	char *ret = kmem_asprintf("{\"%s\", 1=0x%llx, 2=0x%llx, r=%s}",
	    st_string, zh->zhpm_guid_1, zh->zhpm_guid_2, rst_string);
	kmem_strfree(st_string);
	kmem_strfree(rst_string);
	return (ret);
}


nvlist_t *
eh_dep_to_nvlist(const eh_dep_t *ehd)
{
	nvlist_t *nvl = fnvlist_alloc();
	fnvlist_add_uint64(nvl, "eh_last_gen", ehd->eh_last_gen);
	size_t snvls_size = TXG_CONCURRENT_STATES * sizeof(nvlist_t*);
	nvlist_t **snvls = kmem_alloc(snvls_size, KM_SLEEP);
	for (size_t i = 0; i < TXG_CONCURRENT_STATES; i++) {
		const prb_deptrack_count_pair_t *p = &ehd->eh_last_gen_counts[i];
		nvlist_t *pnvl = fnvlist_alloc();
		fnvlist_add_uint64(pnvl, "dtp_txg", p->dtp_txg);
		fnvlist_add_uint64(pnvl, "dtp_count", p->dtp_count);
		snvls[i] = pnvl;
	}
	fnvlist_add_nvlist_array(nvl, "eh_last_gen_counts", snvls, TXG_CONCURRENT_STATES);
	return (nvl);
}

nvlist_t *
entry_header_data_to_nvlist(const entry_header_data_t *ehd)
{
	nvlist_t *ehnvl = fnvlist_alloc();
	fnvlist_add_uint64(ehnvl, "eh_objset_id", ehd->eh_objset_id);
	fnvlist_add_uint64(ehnvl, "eh_zil_guid_1", ehd->eh_zil_guid_1);
	fnvlist_add_uint64(ehnvl, "eh_zil_guid_2", ehd->eh_zil_guid_2);
	fnvlist_add_uint64(ehnvl, "eh_txg", ehd->eh_txg);
	fnvlist_add_uint64(ehnvl, "eh_gen", ehd->eh_gen);
	fnvlist_add_uint64(ehnvl, "eh_gen_scoped_id", ehd->eh_gen_scoped_id);
	fnvlist_add_uint64(ehnvl, "eh_len", ehd->eh_len);
	/* XXX checksums */
	nvlist_t *eh_dep = eh_dep_to_nvlist(&ehd->eh_dep);
	fnvlist_add_nvlist(ehnvl, "eh_dep", eh_dep);
	fnvlist_free(eh_dep);

	return (ehnvl);
}

nvlist_t *
chunk_to_nvlist(const prb_chunk_t *ch)
{
	nvlist_t *nvl = fnvlist_alloc();
	fnvlist_add_uint64(nvl, "ch_base", (uint64_t)ch->ch_base);
	return (nvl);
}

nvlist_t *
replay_node_to_nvlist(const zilpmem_replay_node_t *rn)
{
	nvlist_t *rn_nvl = fnvlist_alloc();
	fnvlist_add_uint64(rn_nvl, "rn_gen", rn->rn_gen);
	fnvlist_add_uint64(rn_nvl, "rn_id", rn->rn_id);
	fnvlist_add_uint64(rn_nvl, "rn_txg", rn->rn_txg);
	nvlist_t *rndepnvl = eh_dep_to_nvlist(&rn->rn_dep);
	fnvlist_add_nvlist(rn_nvl, "rn_dep", rndepnvl);
	fnvlist_free(rndepnvl);

	fnvlist_add_uint64(rn_nvl, "rn_pmem_ptr", (uint64_t)rn->rn_pmem_ptr);
	nvlist_t *chnvl = chunk_to_nvlist(rn->rn_chunk);
	fnvlist_add_nvlist(rn_nvl, "rn_chunk", chnvl);
	fnvlist_free(chnvl);

	return (rn_nvl);
}

static void zilpmem_replay_state_to_phys(const zilpmem_replay_state_t *st,
    zilpmem_replay_state_phys_t *pst)
{
	pst->claim_txg = st->claim_txg;
	pst->resume_state_last = st->resume_state_last;
	zilpmem_do_deptrack_compute_eh_dep_t_from_active(
	    &st->resume_state_active, &pst->resume_state_active);
}

static void zilpmem_replay_state_from_phys(
    const zilpmem_replay_state_phys_t *pst, zilpmem_replay_state_t *st)
{
	/* XXX assertions? */
	st->claim_txg = pst->claim_txg;
	st->resume_state_last = pst->resume_state_last;

	/*
	 * XXX move this to a function that complements
	 * zilpmem_do_deptrack_compute_eh_dep_t_from_active ?
	 */
	st->resume_state_active.dtc_gen = pst->resume_state_active.eh_last_gen;
	for (size_t i = 0; i < TXG_CONCURRENT_STATES; i++) {
		const prb_deptrack_count_pair_t *pp =
		    &pst->resume_state_active.eh_last_gen_counts[i];
		prb_deptrack_count_pair_t *o =
		    &st->resume_state_active.dtc_count[pp->dtp_txg & TXG_MASK];
		*o = *pp;
	}
}


int
zilpmem_replay_node_btree_cmp(const void *va, const void *vb)
{
	const zilpmem_replay_node_t *a = va;
	const zilpmem_replay_node_t *b = vb;
	if (a->rn_gen < b->rn_gen)
		return -1;
	if (a->rn_gen > b->rn_gen)
		return 1;
	ASSERT3U(a->rn_gen, ==, b->rn_gen);
	if (a->rn_id < b->rn_id)
		return -1;
	if (a->rn_id > b->rn_id)
		return 1;
	ASSERT3U(a->rn_id, ==, b->rn_id);
	if (((uintptr_t)a->rn_pmem_ptr) < ((uintptr_t)b->rn_pmem_ptr) )
		return -1;
	if (((uintptr_t)a->rn_pmem_ptr) > ((uintptr_t)b->rn_pmem_ptr) )
		return 1;
	return 0;
}

typedef struct find_replay_nodes_in_chunk_cb_arg {
	zfs_btree_t *btree;
	prb_chunk_t *chunk;
	uint64_t zil_guid_1;
	uint64_t zil_guid_2;
	uint64_t objset_id;
	uint64_t claim_txg;
} find_replay_nodes_in_chunk_cb_arg_t;

static prb_chunk_iter_result_t
find_replay_nodes_in_chunk(prb_chunk_t *chunk, uint64_t zil_guid_1,
    uint64_t zil_guid_2, uint64_t objset_id, uint64_t claim_txg,
    zfs_btree_t *out)
{
	prb_chunk_iter_t iter;
	prb_chunk_iter_init(chunk->ch_base, chunk_len(chunk), &iter);

	const uint8_t *entry_pmem;
	prb_chunk_iter_result_t ires;
	entry_header_t header;
	entry_header_data_t *header_data = &header.eh_data;
	while ((ires = prb_chunk_iter_provided_eh_buf(&iter, &entry_pmem,
	    &header)) == PRB_CHUNK_ITER_OK) {

		if (entry_pmem == NULL) {
			goto ret;
		}

		/* belongs to this HDL? */
		if (!(header_data->eh_zil_guid_1 == zil_guid_1 &&
		    header_data->eh_zil_guid_2 == zil_guid_2 &&
		    header_data->eh_objset_id == objset_id))
		    continue;

		/* obsolete entries can be skipped */
		if (header_data->eh_txg < claim_txg)
		    	continue; /* next */

		zilpmem_replay_node_t rn = {
			.rn_gen = header_data->eh_gen,
			.rn_id = header_data->eh_gen_scoped_id,
			.rn_pmem_ptr = entry_pmem,
			.rn_chunk = chunk,
			.rn_dep = header_data->eh_dep,
			.rn_txg = header_data->eh_txg,
		};
		zfs_btree_index_t where;
		zilpmem_replay_node_t *existing = zfs_btree_find(out, &rn, &where);
		if (existing != NULL) {
			/* We include the rn_pmem_ptr as node ID => this would be a bug in the iterator or this function */
			VERIFY3P(existing->rn_pmem_ptr, !=, rn.rn_pmem_ptr);
			/* FIXME turn this into an error that bubbles up */
			panic("duplicate entry found. Existing at entry_pmem=%px ; new at %p", existing->rn_pmem_ptr, rn.rn_pmem_ptr);
		}
		zfs_btree_add(out, &rn);
	}
ret:
	return (ires);
}

static zfs_btree_t *
zilpmem_new_replay_node_btree(void)
{
	zfs_btree_t *bt = kmem_zalloc(sizeof(zfs_btree_t), KM_SLEEP);
	zfs_btree_create(bt, zilpmem_replay_node_btree_cmp,
	    sizeof(zilpmem_replay_node_t));
	return bt;
}

static zfs_btree_t *
zilpmem_claim_find_all_entries(list_t *chunks, uint64_t zil_guid_1,
    uint64_t zil_guid_2, uint64_t objset_id, uint64_t claim_txg)
{
	zfs_btree_t *bt = zilpmem_new_replay_node_btree();

	/* fill `bt` with any node that we can find for this zil chain */
	for (prb_chunk_t *c = list_head(chunks); c != NULL;
	    c = list_next(chunks, c)) {
		    /* FIXME: It's correct to not bubble up errors here,
		     * but we probably want to inform the user about
		     * MCEs / checksum errors anyways */
		(void) find_replay_nodes_in_chunk(c, zil_guid_1, zil_guid_2, objset_id,
		    claim_txg, bt);
	}
	return (bt);
}

/*
 * Must only be called before or during claiming.
 * Exposed for zdb.
 */
zfs_btree_t *
zilpem_prbh_find_all_entries(zilpmem_prb_handle_t *zph, const zil_header_pmem_impl_t *zh, uint64_t claim_txg)
{
	return (zilpmem_claim_find_all_entries(&zph->zph_prb->waitclaim_chunks,
	    zh->zhpm_guid_1, zh->zhpm_guid_2, zph->zph_objset_id,
	    claim_txg));
}

check_replayable_result_t
zilpmem_replay_resume(zfs_btree_t *bt, zfs_btree_index_t *first_err,
    zilpmem_replay_state_t *state, zilpmem_replay_resume_cb_t cb, void *cb_arg)
{
	check_replayable_result_kind_t ret = CHECK_REPLAYABLE_OK;

	prb_deptrack_count_t *active = &state->resume_state_active;
	eh_dep_t *last = &state->resume_state_last;

	zfs_btree_index_t where;
	zilpmem_replay_node_t *rn = zfs_btree_first(bt, &where);
	for (; rn != NULL; rn = zfs_btree_next(bt, &where, &where)) {

		// XXX this needs to happen after deptrack?
		// otherwise the counters wont't match if we lose
		// any of the unreplayed entries
		if (rn->rn_txg < state->claim_txg ||
		    rn->rn_gen < active->dtc_gen ||
		    (rn->rn_gen == active->dtc_gen && rn->rn_id <= active->dtc_last_id)) {
			/* already replayed */
			continue;
		}

		deptrack_outcome_t outcome;
		outcome = zilpmem_do_deptrack(active, last, rn->rn_txg,
		    rn->rn_gen, rn->rn_id);

		VERIFY(!zilpmem_replay_state_is_init(state));

		switch (outcome) {
			case DEPTRACK_OUTCOME_SAME_GEN:
				/* fallthrough */
			case DEPTRACK_OUTCOME_BEGAN_NEW_GEN:
				/* check that all of the entry's dependencies have been replayed */
				for (size_t i = 0; i < TXG_CONCURRENT_STATES; i++) {
					const prb_deptrack_count_pair_t *rc = &rn->rn_dep.eh_last_gen_counts[i];
					/* dtp_txg=0 marks an unfilled row in the dependency table */
					if (rc->dtp_txg == 0) {
						if (rc->dtp_count != 0) {
							ret = CHECK_REPLAYABLE_INVALID_COUNT_EXPECTED_ZERO;
							goto out;
						}
						continue;
					}
					/*
					 * Ignore dependencies on entries that are older than the claim txg.
					 * Those entries might have already been gc'ed.
					 */
					if (rc->dtp_txg < state->claim_txg) {
						continue;
					}
					/*
					 * Now we've filtered out all the cases where we can ignore the dependency.
					 * => Check that the dependency has been replayed by finding it in `last`.
					 *    (Note that this also applies to `is_first_entry=B_TRUE`: if it's
					 *     legitimately the first entry in a contiguous chain of entries
					 *     its dependencies table will be empty or only contain dependencies
					 *     on entrys from txgs that are older than TXG_CONCURRENT_STATES.)
					 */
					const prb_deptrack_count_pair_t *lc = NULL;
					size_t n = 0;
					for (size_t j = 0; j < TXG_CONCURRENT_STATES; j++) {
						if (rc->dtp_txg == last->eh_last_gen_counts[j].dtp_txg) {
							lc = &last->eh_last_gen_counts[j];
							n++;
						}
					}
					VERIFY3U(n, <=, 1); /* FIXME turn this into an error */
					if (n == 0) {
						ret = CHECK_REPLAYABLE_MISSING_ENTRIES;
						goto out;
					}
					VERIFY(lc);
					if (rc->dtp_count != lc->dtp_count) {
						ret = CHECK_REPLAYABLE_MISSING_ENTRIES;
						goto out;
					}
				}
				break; /* switch */

			case DEPTRACK_OUTCOME_TXG_SHOULD_HAVE_SYNCED_ALREADY:
				/* this cannot happen, we would never have written an obsolete entry */
				ret = CHECK_REPLAYABLE_OBSOLETE_ENTRY_THAT_SHOULD_HAVE_NEVER_BEEN_WRITTEN;
				goto out;
			case DEPTRACK_OUTCOME_ACTIVE_HAS_NEWER_GEN:
				/* since we sort the btree, generations are always monotonic, this cannot happen */
				panic("newer gen: replay btree incorrectly sorted");
			case DEPTRACK_OUTCOME_ACTIVE_HAS_NEWER_ID:
				panic("newer id: replay btree incorrectly sorted");
				goto out;
		}
		VERIFY3U(ret, ==, CHECK_REPLAYABLE_OK);
		zilpmem_replay_resume_cb_result_t res =
		    cb ? cb(cb_arg, rn, state) : ZILPMEM_REPLAY_RESUME_CB_RESULT_NEXT;
		switch (res) {
			case ZILPMEM_REPLAY_RESUME_CB_RESULT_NEXT:
				goto proceed;
			case ZILPMEM_REPLAY_RESUME_CB_RESULT_STOP:
				ret = CHECK_REPLAYABLE_CALLBACK_STOPPED;
				goto  out;
		}
		panic("invalid replay result value %d", res);
proceed:
		(void) 0; /* continue with loop */
	}

	check_replayable_result_t r;
out:
	r.what = ret;
	r.expected_eh_dep = *last;
	r.active = *active;

	memset(&r.offender, 0, sizeof(r.offender));
	if (rn != NULL)
		r.offender = *rn;
	return r;
}

/* XXX compat code, remove it */
check_replayable_result_t
zilpmem_check_replayable(zfs_btree_t *bt, zfs_btree_index_t *first_err, uint64_t claim_txg)
{
	zilpmem_replay_state_t st;
	zilpmem_replay_state_init(&st, claim_txg);
	return zilpmem_replay_resume(bt, first_err, &st, NULL, NULL);
}

static boolean_t
zilpmem_prb_might_claim_during_recovery_impl(
    const zil_header_pmem_impl_t *zh)
{
	switch (zh->zhpm_st) {
		case ZHPM_ST_NOZIL:
			return (B_FALSE);
		case ZHPM_ST_LOGGING:
			return (B_TRUE);
		case ZHPM_ST_REPLAYING:
			return (B_FALSE);
		default:
			VERIFY(zil_header_pmem_state_valid(zh->zhpm_st));
			panic("unreachable");
	}
}

boolean_t
zilpmem_prb_might_claim_during_recovery(const zil_header_pmem_t *zh_opaque)
{
	const zil_header_pmem_impl_t *zh =
	    (const zil_header_pmem_impl_t*)zh_opaque;
	CTASSERT(sizeof(*zh) == sizeof(*zh_opaque));
	return (zilpmem_prb_might_claim_during_recovery_impl(zh));
}

typedef struct {
	zil_header_pmem_state_t st;
	const claimstore_interface_t *claimstore;
	void *claimstore_arg;
	zilpmem_prb_claim_cb_res_t res;
	int claimstore_err;
} zilpmem_prb_claim_cb_arg_t;

static zilpmem_prb_claim_cb_res_t
zilpmem_prb_claim_cb_impl(zilpmem_prb_claim_cb_arg_t *arg,
    const zilpmem_replay_node_t *rn)
{
	boolean_t needs_to_store_claim = B_FALSE;
	arg->claimstore_err = arg->claimstore->prbcsi_needs_store_claim(
	    arg->claimstore_arg, rn, &needs_to_store_claim);
	if (arg->claimstore_err != 0)
		return (CLAIMCB_RES_NEEDS_CLAIMING_ERR);

	if (arg->st == ZHPM_ST_LOGGING) {
		if (!needs_to_store_claim)
			return (CLAIMCB_RES_OK);

		arg->claimstore_err =
		    arg->claimstore->prbcsi_claim(arg->claimstore_arg, rn);
		return (arg->claimstore_err ?
		    CLAIMCB_RES_CLAIM_ERR : CLAIMCB_RES_OK);
	} else {
		VERIFY3U(arg->st, ==, ZHPM_ST_REPLAYING);
		return (needs_to_store_claim ? CLAIMCB_RES_OK :
		    CLAIMCB_RES_ENTRY_NEEDS_CLAIMING_DURING_REPLAY);
	}
}

static zilpmem_replay_resume_cb_result_t
zilpmem_prb_claim_cb(void *varg,
    const zilpmem_replay_node_t *node,
    const zilpmem_replay_state_t *state)
{
	(void) state;
	zilpmem_prb_claim_cb_arg_t *arg = varg;
	arg->res = zilpmem_prb_claim_cb_impl(arg, node);
	return (arg->res == CLAIMCB_RES_OK ?
	    ZILPMEM_REPLAY_RESUME_CB_RESULT_NEXT : ZILPMEM_REPLAY_RESUME_CB_RESULT_STOP);
}

zilpmem_prb_claim_result_t
zilpmem_prb_claim(
    zilpmem_prb_handle_t *zph,
    zil_header_pmem_t *zh_opaque,
    uint64_t pool_first_txg,
    const claimstore_interface_t *claimstore,
    void *claimstore_arg
)
{
	// FIXME concurrency
	VERIFY3S(zph->zph_st, ==, ZPH_ST_ALLOCED);

	zil_header_pmem_impl_t *zh = (zil_header_pmem_impl_t*)zh_opaque;
	zilpmem_prb_claim_result_t ret;

	if (zh->zhpm_st == ZHPM_ST_NOZIL) {
		VERIFY(!zilpmem_prb_might_claim_during_recovery_impl(zh));
		ret.what = PRB_CLAIM_RES_OK;
		zph->zph_st = ZPH_ST_DESTROYED;
		goto out_0;
	}

	if ((zh->zhpm_st & (ZHPM_ST_LOGGING|ZHPM_ST_REPLAYING)) == 0) {
		panic("unknown ZIL-PMEM header state 0x%llx", zh->zhpm_st);
	}


	zilpmem_replay_state_t rst_initial;
	if (zh->zhpm_st == ZHPM_ST_LOGGING) {
		VERIFY(zilpmem_prb_might_claim_during_recovery_impl(zh));
		zilpmem_replay_state_init(&rst_initial, pool_first_txg);
	} else {
		VERIFY3S(zh->zhpm_st, ==, ZHPM_ST_REPLAYING);
		VERIFY(!zilpmem_prb_might_claim_during_recovery_impl(zh));
		zilpmem_replay_state_from_phys(&zh->zhpm_replay_state,
		    &rst_initial);
	}

	/* XXX: rename `rst` to `rst_claim` or `rst_tmp` or similar
	 * since rst_initial is what ultimately lands in `zph`
	 * and `rst` is only used by zilpmem_prb_claim_cb
	 * to do the claiming and/or dry-run of replay.
	 */
	zilpmem_replay_state_t rst = rst_initial;
	zfs_btree_t *cbt;
	cbt = zilpem_prbh_find_all_entries(zph, zh, rst_initial.claim_txg);

	zfs_btree_index_t cbt_idx;
	zilpmem_prb_claim_cb_arg_t arg = {
		.st = zh->zhpm_st,
		.claimstore = claimstore,
		.claimstore_arg = claimstore_arg,
		.claimstore_err = 0,
		.res = CLAIMCB_RES_OK, /* if we don't find an entry */
	};
	check_replayable_result_t cbres = zilpmem_replay_resume(cbt,
	    &cbt_idx, &rst, zilpmem_prb_claim_cb, &arg);

	if (cbres.what == CHECK_REPLAYABLE_OK) {
		VERIFY3S(arg.res, ==, CLAIMCB_RES_OK);
		ret.what = PRB_CLAIM_RES_OK;
		/* we continue below */
	} else if (cbres.what == CHECK_REPLAYABLE_CALLBACK_STOPPED) {
		VERIFY3S(arg.res, !=, CLAIMCB_RES_OK);
		ret.what = PRB_CLAIM_RES_ERR_CLAIMING;
		ret.claiming = arg.res;
		goto out_1;
	} else {
		ret.what = PRB_CLAIM_RES_ERR_STRUCTURAL;
		ret.structural = cbres;
		goto out_1;
	}
	VERIFY3S(cbres.what, ==, CHECK_REPLAYABLE_OK);
	VERIFY3S(arg.res, ==, CLAIMCB_RES_OK);

	VERIFY3S(zph->zph_st, ==, ZPH_ST_ALLOCED);
	zph->zph_st = ZPH_ST_REPLAYING;
	zph->zph_replay_state = rst_initial;
	zph->zph_zil_guid_1 = zh->zhpm_guid_1;
	zph->zph_zil_guid_2 = zh->zhpm_guid_2;

	avl_create(&zph->zph_held_chunks, zilpmem_prb_held_chunk_cmp,
	    sizeof(zilpmem_prb_held_chunk_t),
	    offsetof(zilpmem_prb_held_chunk_t, zphc_avl_node));

	zilpmem_replay_node_t *rn;
	zilpmem_prb_held_chunk_t *hc;
	for (rn = zfs_btree_first(cbt, &cbt_idx); rn != NULL;
	    rn = zfs_btree_next(cbt, &cbt_idx, &cbt_idx)) {
		hc = kmem_zalloc(sizeof(zilpmem_prb_held_chunk_t), KM_SLEEP);
		hc->zphc_chunk = rn->rn_chunk;
		avl_index_t where;
		if (avl_find(&zph->zph_held_chunks, hc, &where) != NULL)
			continue;
		avl_insert(&zph->zph_held_chunks, hc, where);
		/* refcount is decremented in zilpmem_prb_replay_done */
		zfs_refcount_add(&hc->zphc_chunk->ch_rc, zph);
	}

out_1:
	zfs_btree_clear(cbt);
	zfs_btree_destroy(cbt);
out_0:

	if ((zph->zph_st & ~(ZPH_ST_ALLOCED|ZPH_ST_REPLAYING|ZPH_ST_DESTROYED)))
		panic("invalid state %d", zph->zph_st);

	return (ret);
}

typedef struct zilpmem_prb_replay_cb_arg {
	zilpmem_replay_cb_t cb;
	void *cb_arg;
	int cb_err;
	zil_header_pmem_impl_t nhdr;
} zilpmem_prb_replay_cb_arg_t;

static zilpmem_replay_resume_cb_result_t
zilpmem_prb_replay_cb(void *varg,
    const zilpmem_replay_node_t *rn,
    const zilpmem_replay_state_t *state)
{
	zilpmem_prb_replay_cb_arg_t *arg = varg;

	zilpmem_replay_state_to_phys(state, &arg->nhdr.zhpm_replay_state);

	const zil_header_pmem_impl_t *zh = &arg->nhdr;
	const zil_header_pmem_t *zh_opaque = (const zil_header_pmem_t*)zh;

	arg->cb_err = arg->cb(arg->cb_arg, rn, zh_opaque);
	return (arg->cb_err == 0 ?
	    ZILPMEM_REPLAY_RESUME_CB_RESULT_NEXT :
	    ZILPMEM_REPLAY_RESUME_CB_RESULT_STOP);
}

zilpmem_prb_replay_result_t
zilpmem_prb_replay(zilpmem_prb_handle_t *zph, zilpmem_replay_cb_t cb,
    void *cb_arg)
{
	zilpmem_prb_replay_result_t ret;

	// FIXME concurrency
	if(zph->zph_st & ~(ZPH_ST_REPLAYING|ZPH_ST_DESTROYED)) {
		panic("unexpected state %d", zph->zph_st);
	} else if (zph->zph_st == ZPH_ST_DESTROYED) {
		ret.what = PRB_REPLAY_RES_OK;
		goto out;
	}
	VERIFY3S(zph->zph_st, ==, ZPH_ST_REPLAYING);

	zfs_btree_t *rbt = zilpmem_new_replay_node_btree();

	zilpmem_prb_held_chunk_t *hc;
	for (hc = avl_first(&zph->zph_held_chunks);
	    hc != NULL; hc = AVL_NEXT(&zph->zph_held_chunks, hc)) {
		/* XXX VERIFY chunk belongs to this prb */
		/* FIXME: It's correct to not bubble up errors here,
		* but we probably want to inform the user about
		* MCEs / checksum errors anyways */
		(void) find_replay_nodes_in_chunk(hc->zphc_chunk, zph->zph_zil_guid_1,
		    zph->zph_zil_guid_2, zph->zph_objset_id,
		    zph->zph_replay_state.claim_txg,
		    rbt);
	}

	zfs_btree_index_t rbt_idx;
	zilpmem_prb_replay_cb_arg_t arg = {
		.cb = cb,
		.cb_arg = cb_arg,
		.cb_err = 0,
	};
	/* XXX ugly */
	arg.nhdr.zhpm_st = ZHPM_ST_REPLAYING;
	arg.nhdr.zhpm_guid_1 = zph->zph_zil_guid_1;
	arg.nhdr.zhpm_guid_2 = zph->zph_zil_guid_2;

	check_replayable_result_t cbres = zilpmem_replay_resume(rbt,
	    &rbt_idx, &zph->zph_replay_state, zilpmem_prb_replay_cb, &arg);
	if (cbres.what == CHECK_REPLAYABLE_OK) {
		ret.what = PRB_REPLAY_RES_OK;
	} else if (cbres.what == CHECK_REPLAYABLE_CALLBACK_STOPPED) {
		ret.what = PRB_REPLAY_RES_ERR_REPLAYFUNC;
		ret.replayfunc = arg.cb_err;
	} else {
		ret.what = PRB_REPLAY_RES_ERR_STRUCTURAL;
		ret.structural = cbres;
	}

	zfs_btree_clear(rbt);
	zfs_btree_destroy(rbt);
out:

	return (ret);
}

static void
zilpmem_prb_release_and_free_chunkhold(zilpmem_prb_handle_t *zph)
{
	zilpmem_prb_held_chunk_t *hc;
	void *cookie = NULL;
	while ((hc = avl_destroy_nodes(&zph->zph_held_chunks, &cookie))
	    != NULL) {
		prb_chunk_t *chunk = hc->zphc_chunk;
		kmem_free(hc, sizeof(*hc));
		zfs_refcount_remove(&chunk->ch_rc, zph);
		/* zilpmem_prb_gc takes care of cleaning the chunk and
		 * putting it into the free list. That is, unless
		 * zilpmem_prb_t::no_more_gc is set. */
	}
	avl_destroy(&zph->zph_held_chunks);
}

static void
zilpmem_prb_abandon_claim(zilpmem_prb_handle_t *zph,
    zil_header_pmem_t *out_opaque)
{
	VERIFY3P(zph, !=, NULL);
	VERIFY3P(out_opaque, !=, NULL);

	/* FIXME ensure exlusive access */

	zilpmem_prb_release_and_free_chunkhold(zph);

	zph->zph_st = ZPH_ST_DESTROYED;
	zph->zph_zil_guid_1 = 0;
	zph->zph_zil_guid_2 = 0;
	memset(&zph->zph_replay_state, 0, sizeof(zph->zph_replay_state));

	zil_header_pmem_impl_t *out = (zil_header_pmem_impl_t *)out_opaque;
	CTASSERT(sizeof(*out) == sizeof(*out_opaque));
	memset(out, 0, sizeof(*out));
	out->zhpm_st = ZHPM_ST_NOZIL;
	/* FIXME assert valid header */
}

void
zilpmem_prb_replay_done(zilpmem_prb_handle_t *zph,
    zil_header_pmem_t *out_opaque)
{
	// FIXME concurrency
	if (zph->zph_st & ~(ZPH_ST_REPLAYING|ZPH_ST_DESTROYED)) {
		panic("unexpected state %d", zph->zph_st);
	} else if (zph->zph_st & ZPH_ST_DESTROYED) {
		/* FIXME assert zph_zil_guid_* members are 0 */
		/* FIXME dedup code to render this */
		zil_header_pmem_impl_t *out = (zil_header_pmem_impl_t *)out_opaque;
		CTASSERT(sizeof(*out) == sizeof(*out_opaque));
		memset(out, 0, sizeof(*out));
		out->zhpm_st = ZHPM_ST_NOZIL;
		return;
	}
	VERIFY3S(zph->zph_st, ==, ZPH_ST_REPLAYING);
	zilpmem_prb_abandon_claim(zph, out_opaque);
}

static uint64_t
nonzero_u64_random(void)
{
	uint64_t rnd;
	do {
		(void) random_get_pseudo_bytes((uint8_t*)&rnd, sizeof(rnd));
	} while (rnd == 0);
	return rnd;
}

boolean_t
zilpmem_prb_create_log_if_not_exists(zilpmem_prb_handle_t *zph,
    zil_header_pmem_t *out_opaque)
{
	// FIXME concurrency
	if (zph->zph_st & ZPH_ST_LOGGING) {
		return (B_FALSE);
	} else if (zph->zph_st & ~ZPH_ST_DESTROYED) {
		panic("unexpected state %d", zph->zph_st);
	}
	VERIFY3S(zph->zph_st, ==, ZPH_ST_DESTROYED);

	/* log guid needs to be non-zero because zero log guid is defines as sequence terminator */
	zph->zph_zil_guid_1 = nonzero_u64_random();
	zph->zph_zil_guid_2 = nonzero_u64_random();
	/* XXX ensure that there are no collisions => hash set of active log guids in prb */

	prb_deptrack_init(&zph->zph_deptrack);
	zph->zph_st = ZPH_ST_LOGGING;

	zil_header_pmem_impl_t *out = (zil_header_pmem_impl_t *)out_opaque;
	CTASSERT(sizeof(*out) == sizeof(*out_opaque));
	memset(out, 0, sizeof(*out)); // FIXME this was added as a bug fix, we should really always derive hdr state from zph state
	out->zhpm_st = ZHPM_ST_LOGGING;
	out->zhpm_guid_1 = zph->zph_zil_guid_1;
	out->zhpm_guid_2 = zph->zph_zil_guid_2;

	return (B_TRUE);
}

uint64_t
zilpmem_prb_max_written_txg(zilpmem_prb_handle_t *zph)
{
	// FIXME concurrency
	if ((zph->zph_st & ZPH_ST_LOGGING) == 0) {
		return (0);
	}

	uint64_t min, max;
	prb_deptrack_count_minmax_txg(
	    &zph->zph_deptrack.dt_state.resume_state_active, &min, &max);
	return (max);
}

void
zilpmem_prb_destroy_log(zilpmem_prb_handle_t *zph,
    zil_header_pmem_t *out_opaque)
{
	/* any state is allowed */

	zilpmem_prb_release_and_free_chunkhold(zph);

	zph->zph_st = ZPH_ST_DESTROYED;
	zph->zph_zil_guid_1 = 0;
	zph->zph_zil_guid_2 = 0;
	prb_deptrack_fini(&zph->zph_deptrack);

	zil_header_pmem_impl_t *out = (zil_header_pmem_impl_t *)out_opaque;
	CTASSERT(sizeof(*out) == sizeof(*out_opaque));
	memset(out, 0, sizeof(*out));
	out->zhpm_st = ZHPM_ST_NOZIL;
	/* FIXME assert valid header */
}

zilpmem_prb_replay_read_replay_node_result_t
zilpmem_prb_replay_read_replay_node(const zilpmem_replay_node_t *rn,
    entry_header_t *eh,
    uint8_t *body_out,
    size_t body_out_size,
    size_t *body_required_size)
{
	/* FIXME VERIFY / ASSERT alignment requirements for eh, we need them below */
	int err = zfs_pmem_memcpy_mcsafe(eh, rn->rn_pmem_ptr,
	    sizeof(entry_header_t));
	if (err != 0) {
		zfs_dbgmsg("read_replay_node: read header: mce err = %d", err);
		return (READ_REPLAY_NODE_MCE);
	}
// #ifdef ZFS_DEBUG
	/*
	 * FIXME need to re-check everything about the rn.
	 * NB: use entry_body_fletcher4 for speed.
	 */
	zfs_dbgmsg("blindly trusting that replay node with pmem_base=%p has not changed since making replay plan, this has potential for time of check vs time of use", rn->rn_pmem_ptr);
	*body_required_size = eh->eh_data.eh_len;
	if (body_out_size < *body_required_size) {
		return (READ_REPLAY_NODE_ERR_BODY_SIZE_TOO_SMALL);
	}
	VERIFY(body_out); /* only verify here to support 'read just the header' mode where caller passes 0 as body_out_size */
	err = zfs_pmem_memcpy_mcsafe(
	    body_out,
	    rn->rn_pmem_ptr + sizeof(entry_header_t),
	    body_out_size);
	return (err);
// #else
// 	panic("unimpl");
// #endif
}

void zilpmem_prb_init(void)
{
}

void zilpmem_prb_fini(void)
{
}
