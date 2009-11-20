#ifndef	_SYS_BLKDEV_H
#define	_SYS_BLKDEV_H

#ifdef _KERNEL

#include <linux/blkdev.h>
#include <linux/elevator.h>
#include "zfs_config.h"

#ifndef HAVE_BLK_FETCH_REQUEST
static inline struct request *
blk_fetch_request(struct request_queue *q)
{
	struct request *req;

	req = elv_next_request(q);
	if (req)
		blkdev_dequeue_request(req);

	return req;
}
#endif /* HAVE_BLK_FETCH_REQUEST */

#ifndef HAVE_BLK_REQUEUE_REQUEST
static inline void
blk_requeue_request(request_queue_t *q, struct request *req)
{
	elv_requeue_request(q, req);
}
#endif /* HAVE_BLK_REQUEUE_REQUEST */

#ifndef HAVE_BLK_END_REQUEST
static inline bool
blk_end_request(struct request *req, int error, unsigned int nr_bytes)
{
	struct request_queue *q = req->q;
	LIST_HEAD(list);

	/*
	 * Request has already been dequeued but 2.6.18 version of
	 * end_request() unconditionally dequeues the request so we
	 * add it to a local list to prevent hitting the BUG_ON.
	 */
	list_add(&req->queuelist, &list);

	/*
	 * The old API required the driver to end each segment and not
	 * the entire request.  In our case we always need to end the
	 * entire request partial requests are not supported.
	 */
	req->hard_cur_sectors = nr_bytes >> 9;


	spin_lock_irq(q->queue_lock);
	end_request(req, ((error == 0) ? 1 : error));
	spin_unlock_irq(q->queue_lock);

	return 0;
}
#else
# ifdef HAVE_BLK_END_REQUEST_GPL_ONLY
/*
 * Define required to avoid conflicting 2.6.29 non-static prototype for a
 * GPL-only version of the helper.  As of 2.6.31 the helper is available
 * to non-GPL modules and is not explicitly exported GPL-only.
 */
# define blk_end_request ___blk_end_request
static inline bool
___blk_end_request(struct request *req, int error, unsigned int nr_bytes)
{
	struct request_queue *q = req->q;

	/*
	 * The old API required the driver to end each segment and not
	 * the entire request.  In our case we always need to end the
	 * entire request partial requests are not supported.
	 */
	req->hard_cur_sectors = nr_bytes >> 9;

	spin_lock_irq(q->queue_lock);
	end_request(req, ((error == 0) ? 1 : error));
	spin_unlock_irq(q->queue_lock);

	return 0;
}
# endif /* HAVE_BLK_END_REQUEST_GPL_ONLY */
#endif /* HAVE_BLK_END_REQUEST */

#ifndef HAVE_BLK_RQ_POS
static inline sector_t
blk_rq_pos(struct request *req)
{
	return req->sector;
}
#endif /* HAVE_BLK_RQ_POS */

#ifndef HAVE_BLK_RQ_SECTORS
static inline unsigned int
blk_rq_sectors(struct request *req)
{
	return req->nr_sectors;
}
#endif /* HAVE_BLK_RQ_SECTORS */

#if !defined(HAVE_BLK_RQ_BYTES) || defined(HAVE_BLK_RQ_BYTES_GPL_ONLY)
/*
 * Define required to avoid conflicting 2.6.29 non-static prototype for a
 * GPL-only version of the helper.  As of 2.6.31 the helper is available
 * to non-GPL modules in the form of a static inline in the header.
 */
#define blk_rq_bytes __blk_rq_bytes
static inline unsigned int
__blk_rq_bytes(struct request *req)
{
	return blk_rq_sectors(req) << 9;
}
#endif /* !HAVE_BLK_RQ_BYTES || HAVE_BLK_RQ_BYTES_GPL_ONLY */

#ifndef HAVE_GET_DISK_RO
static inline int
get_disk_ro(struct gendisk *disk)
{
	int policy = 0;

	if (disk->part[0])
		policy = disk->part[0]->policy;

	return policy;
}
#endif /* HAVE_GET_DISK_RO */

#ifndef HAVE_RQ_IS_SYNC
static inline bool
rq_is_sync(struct request *req)
{
	return (req->flags & REQ_RW_SYNC);
}
#endif /* HAVE_RQ_IS_SYNC */

#ifndef HAVE_RQ_FOR_EACH_SEGMENT
struct req_iterator {
	int i;
	struct bio *bio;
};

# define for_each_bio(_bio)              \
	for (; _bio; _bio = _bio->bi_next)

# define __rq_for_each_bio(_bio, rq)    \
	if ((rq->bio))                  \
		for (_bio = (rq)->bio; _bio; _bio = _bio->bi_next)

# define rq_for_each_segment(bvl, _rq, _iter)                   \
	__rq_for_each_bio(_iter.bio, _rq)                       \
		bio_for_each_segment(bvl, _iter.bio, _iter.i)
#endif /* HAVE_RQ_FOR_EACH_SEGMENT */

#ifndef DISK_NAME_LEN
#define DISK_NAME_LEN	32
#endif /* DISK_NAME_LEN */

#endif /* KERNEL */

#endif	/* _SYS_BLKDEV_H */
