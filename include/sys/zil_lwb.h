#ifndef _SYS_ZIL_LWB_H_
#define _SYS_ZIL_LWB_H_

#include <sys/zil_impl.h>

/*
 * zh_flags bit settings
 */
#define	ZIL_REPLAY_NEEDED	0x1	/* replay needed - internal only */
#define	ZIL_CLAIM_LR_SEQ_VALID	0x2	/* zh_claim_lr_seq field is valid */

/*
 * Log block chaining.
 *
 * Log blocks are chained together. Originally they were chained at the
 * end of the block. For performance reasons the chain was moved to the
 * beginning of the block which allows writes for only the data being used.
 * The older position is supported for backwards compatibility.
 *
 * The zio_eck_t contains a zec_cksum which for the intent log is
 * the sequence number of this log block. A seq of 0 is invalid.
 * The zec_cksum is checked by the SPA against the sequence
 * number passed in the blk_cksum field of the blkptr_t
 */
typedef struct zil_chain {
	uint64_t zc_pad;
	blkptr_t zc_next_blk;	/* next block in chain */
	uint64_t zc_nused;	/* bytes in log block used */
	zio_eck_t zc_eck;	/* block trailer */
} zil_chain_t;

#define	ZIL_MIN_BLKSZ	4096ULL

/*
 * The words of a log block checksum.
 */
#define	ZIL_ZC_GUID_0	0
#define	ZIL_ZC_GUID_1	1
#define	ZIL_ZC_OBJSET	2
#define	ZIL_ZC_SEQ	3

typedef struct zil_bp_node {
	dva_t		zn_dva;
	avl_node_t	zn_node;
} zil_bp_node_t;


/*
 * Vdev flushing: during a zil_commit(), we build up an AVL tree of the vdevs
 * we've touched so we know which ones need a write cache flush at the end.
 */
typedef struct zil_vdev_node {
	uint64_t	zv_vdev;	/* vdev to be flushed */
	avl_node_t	zv_node;	/* AVL tree linkage */
} zil_vdev_node_t;


#define	ZIL_PREV_BLKS 16


/*
 * Possible states for a given lwb structure.
 *
 * An lwb will start out in the "closed" state, and then transition to
 * the "opened" state via a call to zil_lwb_write_open(). When
 * transitioning from "closed" to "opened" the zilog's "zl_issuer_lock"
 * must be held.
 *
 * After the lwb is "opened", it can transition into the "issued" state
 * via zil_lwb_write_issue(). Again, the zilog's "zl_issuer_lock" must
 * be held when making this transition.
 *
 * After the lwb's write zio completes, it transitions into the "write
 * done" state via zil_lwb_write_done(); and then into the "flush done"
 * state via zil_lwb_flush_vdevs_done(). When transitioning from
 * "issued" to "write done", and then from "write done" to "flush done",
 * the zilog's "zl_lock" must be held, *not* the "zl_issuer_lock".
 *
 * The zilog's "zl_issuer_lock" can become heavily contended in certain
 * workloads, so we specifically avoid acquiring that lock when
 * transitioning an lwb from "issued" to "done". This allows us to avoid
 * having to acquire the "zl_issuer_lock" for each lwb ZIO completion,
 * which would have added more lock contention on an already heavily
 * contended lock.
 *
 * Additionally, correctness when reading an lwb's state is often
 * achieved by exploiting the fact that these state transitions occur in
 * this specific order; i.e. "closed" to "opened" to "issued" to "done".
 *
 * Thus, if an lwb is in the "closed" or "opened" state, holding the
 * "zl_issuer_lock" will prevent a concurrent thread from transitioning
 * that lwb to the "issued" state. Likewise, if an lwb is already in the
 * "issued" state, holding the "zl_lock" will prevent a concurrent
 * thread from transitioning that lwb to the "write done" state.
 */
typedef enum {
    LWB_STATE_CLOSED,
    LWB_STATE_OPENED,
    LWB_STATE_ISSUED,
    LWB_STATE_WRITE_DONE,
    LWB_STATE_FLUSH_DONE,
    LWB_NUM_STATES
} lwb_state_t;


/*
 * Log write block (lwb)
 *
 * Prior to an lwb being issued to disk via zil_lwb_write_issue(), it
 * will be protected by the zilog's "zl_issuer_lock". Basically, prior
 * to it being issued, it will only be accessed by the thread that's
 * holding the "zl_issuer_lock". After the lwb is issued, the zilog's
 * "zl_lock" is used to protect the lwb against concurrent access.
 */
typedef struct lwb {
	zilog_t		*lwb_zilog;	/* back pointer to log struct */
	blkptr_t	lwb_blk;	/* on disk address of this log blk */
	boolean_t	lwb_fastwrite;	/* is blk marked for fastwrite? */
	boolean_t	lwb_slog;	/* lwb_blk is on SLOG device */
	int		lwb_nused;	/* # used bytes in buffer */
	int		lwb_sz;		/* size of block and buffer */
	lwb_state_t	lwb_state;	/* the state of this lwb */
	char		*lwb_buf;	/* log write buffer */
	zio_t		*lwb_write_zio;	/* zio for the lwb buffer */
	zio_t		*lwb_root_zio;	/* root zio for lwb write and flushes */
	dmu_tx_t	*lwb_tx;	/* tx for log block allocation */
	uint64_t	lwb_max_txg;	/* highest txg in this lwb */
	list_node_t	lwb_node;	/* zilog->zl_lwb_list linkage */
	list_t		lwb_itxs;	/* list of itx's */
	list_t		lwb_waiters;	/* list of zil_commit_waiter's */
	avl_tree_t	lwb_vdev_tree;	/* vdevs to flush after lwb write */
	kmutex_t	lwb_vdev_lock;	/* protects lwb_vdev_tree */
	hrtime_t	lwb_issued_timestamp; /* when was the lwb issued? */
} lwb_t;


/*
 * ZIL commit waiter.
 *
 * This structure is allocated each time zil_commit() is called, and is
 * used by zil_commit() to communicate with other parts of the ZIL, such
 * that zil_commit() can know when it safe for it return. For more
 * details, see the comment above zil_commit().
 *
 * The "zcw_lock" field is used to protect the commit waiter against
 * concurrent access. This lock is often acquired while already holding
 * the zilog's "zl_issuer_lock" or "zl_lock"; see the functions
 * zil_process_commit_list() and zil_lwb_flush_vdevs_done() as examples
 * of this. Thus, one must be careful not to acquire the
 * "zl_issuer_lock" or "zl_lock" when already holding the "zcw_lock";
 * e.g. see the zil_commit_waiter_timeout() function.
 */
typedef struct zil_commit_waiter {
	kcondvar_t	zcw_cv;		/* signalled when "done" */
	kmutex_t	zcw_lock;	/* protects fields of this struct */
	list_node_t	zcw_node;	/* linkage in lwb_t:lwb_waiter list */
	lwb_t		*zcw_lwb;	/* back pointer to lwb when linked */
	boolean_t	zcw_done;	/* B_TRUE when "done", else B_FALSE */
	int		zcw_zio_error;	/* contains the zio io_error value */
} zil_commit_waiter_t;


/*
 * Stable storage intent log management structure.  One per dataset.
 */
struct zilog {
	kmutex_t	zl_lock;	/* protects most zilog_t fields */
	struct dsl_pool	*zl_dmu_pool;	/* DSL pool */
	spa_t		*zl_spa;	/* handle for read/write log */
	const zil_header_t *zl_header;	/* log header buffer */
	objset_t	*zl_os;		/* object set we're logging */
	zil_get_data_t	*zl_get_data;	/* callback to get object content */
	lwb_t		*zl_last_lwb_opened; /* most recent lwb opened */
	hrtime_t	zl_last_lwb_latency; /* zio latency of last lwb done */
	uint64_t	zl_lr_seq;	/* on-disk log record sequence number */
	uint64_t	zl_commit_lr_seq; /* last committed on-disk lr seq */
	uint64_t	zl_destroy_txg;	/* txg of last zil_destroy() */
	uint64_t	zl_replayed_seq[TXG_SIZE]; /* last replayed rec seq */
	uint64_t	zl_replaying_seq; /* current replay seq number */
	uint32_t	zl_suspend;	/* log suspend count */
	kcondvar_t	zl_cv_suspend;	/* log suspend completion */
	uint8_t		zl_suspending;	/* log is currently suspending */
	uint8_t		zl_keep_first;	/* keep first log block in destroy */
	uint8_t		zl_replay;	/* replaying records while set */
	uint8_t		zl_stop_sync;	/* for debugging */
	kmutex_t	zl_issuer_lock;	/* single writer, per ZIL, at a time */
	uint8_t		zl_logbias;	/* latency or throughput */
	uint8_t		zl_sync;	/* synchronous or asynchronous */
	zil_parse_result_t zl_last_parse_result; /* last zil_parse() result */
	itxg_t		zl_itxg[TXG_SIZE]; /* intent log txg chains */
	list_t		zl_itx_commit_list; /* itx list to be committed */
	uint64_t	zl_cur_used;	/* current commit log size used */
	list_t		zl_lwb_list;	/* in-flight log write list */
	avl_tree_t	zl_bp_tree;	/* track bps during log parse */
	clock_t		zl_replay_time;	/* lbolt of when replay started */
	uint64_t	zl_replay_blks;	/* number of log blocks replayed */
	zil_header_t	zl_old_header;	/* debugging aid */
	uint_t		zl_prev_blks[ZIL_PREV_BLKS]; /* size - sector rounded */
	uint_t		zl_prev_rotor;	/* rotor for zl_prev[] */
	txg_node_t	zl_dirty_link;	/* protected by dp_dirty_zilogs list */
	uint64_t	zl_dirty_max_txg; /* highest txg used to dirty zilog */
	/*
	 * Max block size for this ZIL.  Note that this can not be changed
	 * while the ZIL is in use because consumers (ZPL/zvol) need to take
	 * this into account when deciding between WR_COPIED and WR_NEED_COPY
	 * (see zil_max_copied_data()).
	 */
	uint64_t	zl_max_block_size;
};

void zil_commit_waiter_skip(zil_commit_waiter_t *zcw);


/*
 * Used for zil kstat.
 */
typedef struct zil_stats {
	/*
	 * Number of times a ZIL commit (e.g. fsync) has been requested.
	 */
	kstat_named_t zil_commit_count;

	/*
	 * Number of times the ZIL has been flushed to stable storage.
	 * This is less than zil_commit_count when commits are "merged"
	 * (see the documentation above zil_commit()).
	 */
	kstat_named_t zil_commit_writer_count;

	/*
	 * Number of transactions (reads, writes, renames, etc.)
	 * that have been committed.
	 */
	kstat_named_t zil_itx_count;

	/*
	 * See the documentation for itx_wr_state_t above.
	 * Note that "bytes" accumulates the length of the transactions
	 * (i.e. data), not the actual log record sizes.
	 */
	kstat_named_t zil_itx_indirect_count;
	kstat_named_t zil_itx_indirect_bytes;
	kstat_named_t zil_itx_copied_count;
	kstat_named_t zil_itx_copied_bytes;
	kstat_named_t zil_itx_needcopy_count;
	kstat_named_t zil_itx_needcopy_bytes;

	/*
	 * Transactions which have been allocated to the "normal"
	 * (i.e. not slog) storage pool. Note that "bytes" accumulate
	 * the actual log record sizes - which do not include the actual
	 * data in case of indirect writes.
	 */
	kstat_named_t zil_itx_metaslab_normal_count;
	kstat_named_t zil_itx_metaslab_normal_bytes;

	/*
	 * Transactions which have been allocated to the "slog" storage pool.
	 * If there are no separate log devices, this is the same as the
	 * "normal" pool.
	 */
	kstat_named_t zil_itx_metaslab_slog_count;
	kstat_named_t zil_itx_metaslab_slog_bytes;
} zil_stats_t;

extern zil_stats_t zil_stats;

#define	ZIL_STAT_INCR(stat, val) \
    atomic_add_64(&zil_stats.stat.value.ui64, (val));
#define	ZIL_STAT_BUMP(stat) \
    ZIL_STAT_INCR(stat, 1);


extern void	zil_lwb_add_block(struct lwb *lwb, const blkptr_t *bp);
extern void	zil_lwb_add_txg(struct lwb *lwb, uint64_t txg);


#endif /* _SYS_ZIL_LWB_H_ */
