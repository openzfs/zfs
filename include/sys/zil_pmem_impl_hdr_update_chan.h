#ifndef _ZIL_PMEM_IMPL_HDR_UPDATE_CHAN_H_
#define _ZIL_PMEM_IMPL_HDR_UPDATE_CHAN_H_

#include <sys/zfs_context.h>

#include <sys/zil_pmem_prb.h>

typedef struct hdr_update {
	uint64_t txg;
	zil_header_pmem_t upd;
} hdr_update_t;

typedef struct hdr_update_chan {
	kmutex_t mtx;
	uint64_t max_send_txg;
	hdr_update_t upds[TXG_SIZE];
	void *senders[TXG_SIZE];
} hdr_update_chan_t;

static inline void hdr_update_chan_ctor(hdr_update_chan_t *c)
{
	memset(c, 0, sizeof(*c));
	mutex_init(&c->mtx, NULL, MUTEX_DEFAULT, NULL);
}

static inline void hdr_update_chan_dtor(hdr_update_chan_t *c)
{
	mutex_destroy(&c->mtx);
}

static inline void _hdr_update_chan_send(hdr_update_chan_t *c, hdr_update_t u, void *tag)
{
	ASSERT(MUTEX_HELD(&c->mtx));

	VERIFY3U(u.txg, >, 0);
	hdr_update_t *cell = &c->upds[u.txg & TXG_MASK];
	IMPLY((cell->txg != 0), (cell->txg & TXG_MASK) == (u.txg & TXG_MASK));
	VERIFY3U(cell->txg, <=, u.txg);
	/* XXX assert u.upd is valid header */
	c->max_send_txg = MAX(c->max_send_txg, u.txg);
	*cell = u;
	c->senders[u.txg & TXG_MASK] = tag;
}

static inline boolean_t hdr_update_chan_get_for_sync(hdr_update_chan_t *c,
						     uint64_t txg, zil_header_pmem_t *out)
{
	mutex_enter(&c->mtx);

	VERIFY3U(txg, >, 0); /* we use .txg = 0 to encode nonexistence */

	size_t nmatches = 0;
	for (size_t i = 0; i < TXG_SIZE; i++)
	{
		const hdr_update_t *cell = &c->upds[i];
		/* assert _hdr_update_chan_send */
		/* Assert all previous txgs called this function */

		if (cell->txg < txg)
		{
			/* we set this below */
			VERIFY3U(cell->txg, ==, 0);
		}
		else
		{
			VERIFY3U((cell->txg & TXG_MASK), ==, i);
			if (cell->txg == txg)
			{
				nmatches++;
			}
			else
			{
				VERIFY3U(cell->txg, >, txg);
			}
		}
	}
	VERIFY3U(nmatches, <=, 1); /* if not likely error in _hdr_update_chan_send */

	hdr_update_t *cell = &c->upds[txg & TXG_MASK];
	boolean_t has_update;
	if (cell->txg != 0)
	{
		VERIFY3U(cell->txg, ==, txg);
		*out = cell->upd;
		cell->txg = 0;
		has_update = B_TRUE;
		c->senders[txg & TXG_MASK] = NULL;
	}
	else
	{
		VERIFY0(c->senders[txg & TXG_MASK]);
		has_update = B_FALSE;
	}

	mutex_exit(&c->mtx);

	return (has_update);
}

#endif /* _ZIL_PMEM_IMPL_HDR_UPDATE_CHAN_H_ */
