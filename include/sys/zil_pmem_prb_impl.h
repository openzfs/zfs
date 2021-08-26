#ifndef _zil_pmem_prb_IMPL_H_
#define _zil_pmem_prb_IMPL_H_

#include <sys/zil_pmem_prb.h>

typedef struct zilpmem_replay_state
{
	uint64_t claim_txg;
	prb_deptrack_count_t resume_state_active;
	eh_dep_t resume_state_last;
} zilpmem_replay_state_t;

check_replayable_result_t
zilpmem_check_replayable(zfs_btree_t *bt, zfs_btree_index_t *first_err, uint64_t claim_txg);

typedef enum zilpmem_replay_resume_cb_result
{
	ZILPMEM_REPLAY_RESUME_CB_RESULT_NEXT = 1,
	ZILPMEM_REPLAY_RESUME_CB_RESULT_STOP = 2,
} zilpmem_replay_resume_cb_result_t;

typedef zilpmem_replay_resume_cb_result_t (*zilpmem_replay_resume_cb_t)(void *cb_arg,
									const zilpmem_replay_node_t *node,
									const zilpmem_replay_state_t *state);

check_replayable_result_t
zilpmem_replay_resume(zfs_btree_t *bt, zfs_btree_index_t *first_err,
		      zilpmem_replay_state_t *state, zilpmem_replay_resume_cb_t cb, void *cb_arg);

typedef struct zilpmem_replay_state_phys
{
	uint64_t claim_txg;
	eh_dep_t resume_state_active;
	eh_dep_t resume_state_last;
} zilpmem_replay_state_phys_t;

void zilpmem_replay_state_init(zilpmem_replay_state_t *s, uint64_t claim_txg);
char *zilpmem_replay_state_phys_debug_string(const zilpmem_replay_state_phys_t *s);

typedef enum zil_header_pmem_state
{
	/* start at 1 to distinguish invalid zero state from nozil */
	ZHPM_ST_NOZIL = 1,
	ZHPM_ST_LOGGING = 2,
	ZHPM_ST_REPLAYING = 3,
} zil_header_pmem_state_t;

/* NULL indicates invalid */
const char *zil_header_pmem_state_debug_str(zil_header_pmem_state_t s);

boolean_t zil_header_pmem_state_valid(uint64_t st);
void zil_header_pmem_state_from_header(const zil_header_pmem_t *zh,
				       zil_header_pmem_state_t *out, boolean_t *valid); /* no global effects */
void zil_header_pmem_claimtxg_from_header(const zil_header_pmem_t *zh,
					  uint64_t *claim_txg, boolean_t *valid); /* no global effects */

typedef struct zil_header_pmem_impl
{
	uint64_t zhpm_st; /* zil_header_pmem_state_t */
	uint64_t zhpm_guid_1;
	uint64_t zhpm_guid_2;
	zilpmem_replay_state_phys_t zhpm_replay_state;
	// TODO max log record in order to support claimstore / WR_INDIRECT
} zil_header_pmem_impl_t;

CTASSERT_GLOBAL(sizeof(zil_header_pmem_t) == sizeof(zil_header_pmem_impl_t));

/* for use with zfs_btree_t, exported for testing purposes */
int zilpmem_replay_node_btree_cmp(const void *va, const void *vb);

struct prb_chunk
{
	zfs_refcount_t ch_rc;
	list_node_t ch_all_list_node;
	list_node_t ch_current_list_node;
	uint8_t *ch_base;
	uint8_t *ch_cur;
	uint8_t *ch_end;  /* exclusive, i.e. ch_end - ch_base = len */
	uint64_t max_txg; /* FIXME review this is useful & correctly updated => test coverage or delete */
};

boolean_t
prb_chunk_contains_ptr(const prb_chunk_t *c, const uint8_t *p);

typedef enum
{
	WRITE_CHUNK_OK,
	WRITE_CHUNK_ENOSPACE,
} prb_write_raw_chunk_result_t;

prb_write_raw_chunk_result_t prb_write_chunk(prb_chunk_t *entry_chunk, uint64_t objset_id,
					     uint64_t zil_guid_1,
					     uint64_t zil_guid_2, uint64_t txg, uint64_t gen, uint64_t gen_scoped_id,
					     eh_dep_t dep, const uint8_t *body_dram, size_t body_len,
					     entry_header_t *staging_header,
					     uint8_t *staging_last_256b_block,
					     prb_write_stats_t *stats_out);

typedef struct prb_chunk_iter
{
	uint8_t const *cur;
	uint8_t const *end;
} prb_chunk_iter_t;

void prb_chunk_iter_init(const uint8_t *base_pmem, size_t len,
			 prb_chunk_iter_t *w);

typedef enum prb_chunk_iter_result
{
	PRB_CHUNK_ITER_OK,
	PRB_CHUNK_ITER_ERR_MCE,
	PRB_CHUNK_ITER_ERR_HDR_CHECSUM,
	PRB_CHUNK_ITER_ERR_INVALID_LEN,
	PRB_CHUNK_ITER_ERR_INVALID_LOG_GUID,
	PRB_CHUNK_ITER_ERR_BODY_OUT_OF_BOUNDS,
} prb_chunk_iter_result_t;

prb_chunk_iter_result_t
prb_chunk_iter(prb_chunk_iter_t *w, uint8_t const **entry);

list_t *
zilpmem_prb_all_chunks(zilpmem_prb_t *prb);

zfs_btree_t *
zilpem_prbh_find_all_entries(zilpmem_prb_handle_t *zph, const zil_header_pmem_impl_t *zh, uint64_t claim_txg);

typedef struct zilpmem_prb_held_chunk
{
	avl_node_t zphc_avl_node;
	prb_chunk_t *zphc_chunk;
} zilpmem_prb_held_chunk_t;

static inline int
zilpmem_prb_held_chunk_cmp(const void *va, const void *vb)
{
	const zilpmem_prb_held_chunk_t *a = va;
	const zilpmem_prb_held_chunk_t *b = vb;
	return (TREE_PCMP(a->zphc_chunk, b->zphc_chunk));
}

/* FIXME put this somewhere global, refactor macros to rely on consteval
 * of compiler
 */
#ifdef ZFS_DEBUG
#define PRB_WITH_ASSERT (1)
#define PRB_DEBUG_NOINLINE noinline
#else
#define PRB_WITH_ASSERT (0)
#define PRB_DEBUG_NOINLINE
#endif

typedef struct prb_deptrack
{
	spl_spinlock_t dt_sl;
	zilpmem_replay_state_t dt_state;
#if PRB_WITH_ASSERT == 1
	uint64_t dt_dbg_active_prb_write;
#endif
} prb_deptrack_t;

typedef enum
{
	/* Start at 0x1 to catch missing initialization if we use zalloc */
	ZPH_ST_ALLOCED = 1 << 0,
	ZPH_ST_REPLAYING = 1 << 1,
	ZPH_ST_DESTROYED = 1 << 2,
	ZPH_ST_LOGGING = 1 << 3,
	ZPH_ST_FREED = 1 << 4,
} zilpmem_prb_handle_state_t;

struct zilpmem_prb_handle
{
	avl_node_t zph_avl_node;
	zilpmem_prb_t *zph_prb;
	zilpmem_prb_handle_state_t zph_st;
	uint64_t zph_objset_id;

	/* ZPH_ST_LOGGING|ZPH_ST_REPLAY-only */

	uint64_t zph_zil_guid_1;
	uint64_t zph_zil_guid_2;

	/* ZPH_ST_LOGGGING-only: */
	prb_deptrack_t zph_deptrack;

	/* ZPH_ST_REPLAY-only: */
	avl_tree_t zph_held_chunks; /* zilpmem_prb_held_chunk_t */
	zilpmem_replay_state_t zph_replay_state;
};

struct zilpmem_replay_state_phys_impl
{
	uint64_t cur_gen;
	uint64_t cur_id;
	eh_dep_t cur_count;
	uint64_t last_gen;
	eh_dep_t last_count;
};

/* FIXME */
#define CACHELINE_LEN 64

/*
 * Per-committer in-DRAM data structure that contains per-commiter state.
 */
typedef struct prb_committer
{

	prb_chunk_t *chunk; /* the committer's current chunk */

	/* re-usable buffers for prb_write */

	entry_header_t *staging_header;
	void *staging_last_256b_block;

	/* Padding to avoid cache-line ping-pong */

	uint8_t _pad[CACHELINE_LEN - sizeof(prb_chunk_t *) - sizeof(entry_header_t *) - sizeof(void *)];
} prb_committer_t;

typedef struct committer_slot
{
	size_t cs_cs;
} committer_slot_t;

#define MAX_COMMITTER_SLOTS 64 // 64 bits in an uint64

struct zilpmem_prb
{
	/* Chunks */
	kmutex_t chunk_mtx;
	uint64_t min_chunk_size;
	kcondvar_t chunk_cond;
	list_t all_chunks;
	list_t waitclaim_chunks;
	list_t free_chunks;
	list_t full_chunks[TXG_SIZE];
	list_t claimed_chunks; // TODO remove
	avl_tree_t handles;
	int promised_no_more_gc;

	/* Comitter slots */

	size_t ncommitters; /* immutable */
	struct
	{
		spl_sem_t committer_sem;
		/* each of the lower this.ncommitters bits represent a committer slot */
		uint64_t committer_slots;
	} committer_slot_distribution __attribute__((aligned(CACHELINE_LEN)));
	prb_committer_t *committer; /* Per-commiter in-DRAM state. Size ncommitters */
};

#endif /* _zil_pmem_prb_IMPL_H_ */
