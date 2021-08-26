#ifndef _zil_pmem_prb_H_
#define _zil_pmem_prb_H_

#include <sys/zfs_context.h>
#include <sys/txg.h>
#include <sys/zil.h>

void zilpmem_prb_init(void);
void zilpmem_prb_fini(void);

typedef struct prb_chunk prb_chunk_t;
typedef struct zilpmem_prb zilpmem_prb_t;
typedef struct zilpmem_prb_handle zilpmem_prb_handle_t;

/* Initialize an empty prb. */
zilpmem_prb_t *zilpmem_prb_alloc(size_t ncommitters);

/*
 * Free the in-DRAM zilpmem_prb_t.
 *
 * Does not modify any PMEM state.
 *
 * Ownership of all prb_chunk_t added to the PRB moves to the caller unless
 * free_chunk == B_TRUE in which case the chunks are freed using prb_chunk_free.
 *
 * Panics if there are still handles that have not been torn down by
 * zilpmem_prb_teardown_objset().
 */
void zilpmem_prb_free(zilpmem_prb_t *b, boolean_t free_chunks);

/* panics if an objset with this id is already set up */
zilpmem_prb_handle_t *
zilpmem_prb_setup_objset(zilpmem_prb_t *prb, uint64_t objset_id);

/*
 * Promise that no more calls to zilpmem_prb_gc() will be made.
 * If zilpmem_prb_gc() is called regardless it will panic.
 * Only useful in combination with zilpmem_prb_teardown_objset().
 */
void zilpmem_prb_promise_no_more_gc(zilpmem_prb_t *prb);

/*
 * If abandon_claim == B_FALSE
 *  - zilpmem_prb_promise_no_more_gc() must have been called before
 *  - no changes to PMEM will be made
 *  - upd must be NULL.
 * Otherwise (abandon_claim == B_TRUE)
 *  - upd must be non-NULL and persisted by the caller.
 *  - all chunks with claimed entries become immediately available for
 *    gc unless referenced by other claims
 *  - it is the caller's responsibility to clean up the claimstore
 *
 * => EQUIV(abandon_claim, upd == NULL)
 */
void zilpmem_prb_teardown_objset(zilpmem_prb_handle_t *zph,
				 boolean_t abandon_claim, zil_header_pmem_t *upd);

void zil_header_pmem_init(zil_header_pmem_t *zh);
boolean_t zil_header_pmem_validate_format(const zil_header_pmem_t *zh);

boolean_t zilpmem_prb_might_claim_during_recovery(const zil_header_pmem_t *zh);

prb_chunk_t *prb_chunk_alloc(uint8_t *pmem_base, size_t len);
void prb_chunk_free(prb_chunk_t *c);

/*
 * Add the chunk to the prb for immediate use for writing. The function
 * initializes the chunks synchronously and then adds them to the prb's free
 * list.
 *
 * It is the caller's responsibility to ensure that the memory area of the
 * given chunks has not been added to this or any other concurrencly existing
 * prb instance. There are currently no assertions for this.
 *
 * This function moves ownership of the chunk from the caller to the prb.
 * Neither the prb_chunk_t nor the chunk of PMEM it points to must be accessed
 * until the prb has been freed using zilpmem_prb_free().
 */
void zilpmem_prb_add_chunk_for_write(zilpmem_prb_t *prb, prb_chunk_t *chunk);

/*
 * Add the chunk to the prb for claiming. The chunks are not available for write
 * until claiming is complete. This function panics if claiming is already
 * complete.
 *
 * The chunks added using this method must have been previously added to a prior
 * instance of the prb using zilpmem_prb_add_chunks_for_write().
 * There are no means to assert this, so just do it correctly.
 * "Successfully" means that zilpmem_prb_add_chunks_for_write() returned 0.
 *
 * This function does not alter PMEM state.
 *
 * This function moves ownership of the chunk from the caller to the prb.
 * Neither the prb_chunk_t nor the chunk of PMEM it points to must be accessed
 * until the prb has been freed using zilpmem_prb_free().
 */
void zilpmem_prb_add_chunk_for_claim(zilpmem_prb_t *prb, prb_chunk_t *chunk);

int zilpmem_prb_write_entry(
    zilpmem_prb_handle_t *b,
    uint64_t txg,
    boolean_t needs_new_gen,
    size_t body_len,
    const void *body_dram);

typedef enum
{
	PRB_WRITE_OK,
	PRB_WRITE_OBSOLETE,
	PRB_WRITE_EWOULDSLEEP,
} prb_write_result_t;

typedef struct prb_write_stats
{
	uint64_t get_committer_slot_nanos;
	uint64_t put_committer_slot_nanos;
	uint64_t dt_sl_aquisition_nanos;
	uint64_t dt_sl_held_nanos;
	uint64_t pmem_nanos;

	size_t get_chunk_calls;
	size_t get_chunk_calls_sleeps;
	size_t obsolete;
	size_t beginning_new_gen;
	size_t committer_slot;

	prb_chunk_t *entry_chunk;
	uint8_t *entry_pmem_base;
} prb_write_stats_t;

prb_write_result_t zilpmem_prb_write_entry_with_stats(
    zilpmem_prb_handle_t *b,
    uint64_t txg,
    boolean_t needs_new_gen,
    size_t body_len,
    const void *body_dram,
    boolean_t may_sleep,
    prb_write_stats_t *stats_out);

/* returns 0 if nothing was written through this handle */
uint64_t zilpmem_prb_max_written_txg(zilpmem_prb_handle_t *zph);

void zilpmem_prb_destroy_log(zilpmem_prb_handle_t *b, zil_header_pmem_t *out);

void zilpmem_prb_gc(
    zilpmem_prb_t *b,
    uint64_t synced_txg);

typedef struct prb_deptrack_txg_seq_pair
{
	uint64_t dtp_txg;   /* 0 <=> invalid pair */
	uint64_t dtp_count; /* 0 <=> invalid pair */
} prb_deptrack_count_pair_t;

char *prb_deptrack_count_pair_debug_string(const prb_deptrack_count_pair_t *p);

typedef struct eh_dep
{
	uint64_t eh_last_gen; // FIXME need not be repated in entry headers, only relevant for replay
	prb_deptrack_count_pair_t eh_last_gen_counts[TXG_CONCURRENT_STATES];
} eh_dep_t;

char *eh_dep_t_debug_string(const eh_dep_t *eh);

typedef struct entry_header_data_t
{
	uint64_t eh_objset_id;
	uint64_t eh_zil_guid_1;
	uint64_t eh_zil_guid_2;
	uint64_t eh_txg;
	uint64_t eh_gen;
	uint64_t eh_gen_scoped_id;
	uint64_t eh_len;
	zio_cksum_t eh_body_csum;
	zio_cksum_t eh_header_csum;
	eh_dep_t eh_dep; /* XXX implement the compact encoding presented in the thesis */
} entry_header_data_t;

CTASSERT_GLOBAL(sizeof(entry_header_data_t) == (7 + 2 * 4 + (1 + TXG_CONCURRENT_STATES * 2)) * sizeof(uint64_t));

/* In-PMEM representation of an entry header */
typedef struct entry_header
{
	entry_header_data_t eh_data; /* must be 8-byte aligned so stores are atomics */
	uint8_t eh_pad[256 - 1 * sizeof(entry_header_data_t)];
} entry_header_t;

typedef int(prb_walk_cb_t)(const uint8_t *pmem_base, const entry_header_data_t *header_data,
			   const uint8_t *pmem_body, size_t body_len, void *arg);
void prb_walk_phys(const uint8_t *base, size_t chunklen, size_t numchunks,
		   prb_walk_cb_t cb, void *arg);

/*
 * Create a log if it does not already exists.
 *
 * Panics if !zilpmem_prb_replay_is_done()
 */
boolean_t zilpmem_prb_create_log_if_not_exists(zilpmem_prb_handle_t *zph,
					       zil_header_pmem_t *upd);

typedef struct zilpmem_replay_node zilpmem_replay_node_t;

typedef enum
{
	READ_REPLAY_NODE_OK,
	READ_REPLAY_NODE_MCE,
	READ_REPLAY_NODE_ERR_CHECKSUM,
	READ_REPLAY_NODE_ERR_BODY_SIZE_TOO_SMALL,
} zilpmem_prb_replay_read_replay_node_result_t;

zilpmem_prb_replay_read_replay_node_result_t
zilpmem_prb_replay_read_replay_node(const zilpmem_replay_node_t *rn,
				    entry_header_t *eh,
				    uint8_t *body_out,
				    size_t body_out_size,
				    size_t *body_required_size);

typedef enum check_replayable_result_kind
{
	CHECK_REPLAYABLE_OK,
	CHECK_REPLAYABLE_CALLBACK_STOPPED,
	CHECK_REPLAYABLE_INVALID_COUNT_EXPECTED_ZERO,
	CHECK_REPLAYABLE_MISSING_TXG,
	CHECK_REPLAYABLE_MISSING_ENTRIES,
	CHECK_REPLAYABLE_OBSOLETE_ENTRY_THAT_SHOULD_HAVE_NEVER_BEEN_WRITTEN,
} check_replayable_result_kind_t;

typedef struct prb_deptrack_count
{
	uint64_t dtc_gen;
	uint64_t dtc_last_id;
	prb_deptrack_count_pair_t dtc_count[TXG_SIZE];
} prb_deptrack_count_t;

struct zilpmem_replay_node
{
	/* ordering */
	uint64_t rn_gen;
	uint64_t rn_id;
	const uint8_t *rn_pmem_ptr;

	/* not part of ordering */
	prb_chunk_t *rn_chunk;
	eh_dep_t rn_dep;
	uint64_t rn_txg;
};

typedef struct check_replayable_result
{
	check_replayable_result_kind_t what;
	prb_deptrack_count_t active;
	eh_dep_t expected_eh_dep;
	zilpmem_replay_node_t offender;
} check_replayable_result_t;

typedef enum
{
	CLAIMCB_RES_OK,
	CLAIMCB_RES_NEEDS_CLAIMING_ERR,
	CLAIMCB_RES_CLAIM_ERR,
	CLAIMCB_RES_ENTRY_NEEDS_CLAIMING_DURING_REPLAY,
} zilpmem_prb_claim_cb_res_t;

typedef struct
{
	int (*prbcsi_needs_store_claim)(void *arg, const zilpmem_replay_node_t *rn,
					boolean_t *needs_to_store_claim);
	int (*prbcsi_claim)(void *arg, const zilpmem_replay_node_t *rn);
} claimstore_interface_t;

typedef enum zilpmem_prb_claim_result_kind
{
	PRB_CLAIM_RES_OK,
	PRB_CLAIM_RES_ERR_STRUCTURAL,
	PRB_CLAIM_RES_ERR_CLAIMING,
} zilpmem_prb_claim_result_kind_t;

typedef struct zilpmem_prb_claim_result
{
	zilpmem_prb_claim_result_kind_t what;
	union
	{
		check_replayable_result_t structural;
		zilpmem_prb_claim_cb_res_t claiming;
	};
} zilpmem_prb_claim_result_t;

zilpmem_prb_claim_result_t
zilpmem_prb_claim(
    zilpmem_prb_handle_t *zph,
    zil_header_pmem_t *zh_opaque,
    uint64_t pool_first_txg,
    const claimstore_interface_t *claimstore,
    void *claimstore_arg);

typedef enum zilpmem_prb_replay_result_kind
{
	PRB_REPLAY_RES_OK,
	PRB_REPLAY_RES_ERR_STRUCTURAL,
	PRB_REPLAY_RES_ERR_REPLAYFUNC,
} zilpmem_prb_replay_result_kind_t;

typedef struct zilpmem_prb_replay_result
{
	zilpmem_prb_replay_result_kind_t what;
	union
	{
		check_replayable_result_t structural;
		int replayfunc;
	};
} zilpmem_prb_replay_result_t;

typedef int (*zilpmem_replay_cb_t)(void *rarg,
				   const zilpmem_replay_node_t *rn,
				   const zil_header_pmem_t *upd);

zilpmem_prb_replay_result_t
zilpmem_prb_replay(zilpmem_prb_handle_t *zph, zilpmem_replay_cb_t cb,
		   void *cb_arg);

void zilpmem_prb_replay_done(zilpmem_prb_handle_t *zph, zil_header_pmem_t *upd);

boolean_t zilpmem_prb_replay_is_done(zilpmem_prb_handle_t *zph);

char *zil_header_pmem_debug_string(const zil_header_pmem_t *zh);

nvlist_t *eh_dep_to_nvlist(const eh_dep_t *ehd);
nvlist_t *entry_header_data_to_nvlist(const entry_header_data_t *ehd);
nvlist_t *chunk_to_nvlist(const prb_chunk_t *ch);
nvlist_t *replay_node_to_nvlist(const zilpmem_replay_node_t *rn);

#endif /* _zil_pmem_prb_H_ */
