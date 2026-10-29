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
 * Copyright (c) 2026, Patrick Domack.
 */

#include <sys/zfs_context.h>
#include <sys/zfs_rlock.h>
#include <pthread.h>
#include <unistd.h>

#include "unit.h"

/*
 * The racy tests use a file NPG pages long.  Writers lock whole pages
 * and readers lock any bytes, so a reader overlaps a writer exactly
 * when they share a page, whether the reader holds a slot or a tree
 * entry.  Shadow counters record who is inside each page, so an
 * exclusion failure shows up as a violation.  The racy tests run
 * until every race they need has been seen or a violation shows up,
 * and fail if a race was not seen within MAX_RUN_NS.
 */
#define	PG		4096ULL
#define	NPG		64
#define	MAXTHR		16
#define	NRACE		8
#define	WATCHDOG_S	120
#define	WAIT_NS		SEC2NSEC(30)
#define	STALL_NS	SEC2NSEC(20)
#define	MIN_RUN_NS	MSEC2NSEC(200)
#define	MAX_RUN_NS	SEC2NSEC(30)

enum {
	K_FAST, K_TREE, K_WRITER, K_TRY, K_DEDUPE, K_NESTED, NKIND
};

static const char *kind_name[NKIND] = {
	"fast", "tree", "writer", "try", "dedupe", "nested"
};

typedef struct rl_test {
	zfs_rangelock_t	rt_rl;
	uint32_t	rt_rd[NPG];	/* readers in each page */
	uint32_t	rt_wr[NPG];	/* writers in each page */
	uint32_t	rt_stop;
	boolean_t	rt_whole;	/* some writes whole-file */
	boolean_t	rt_grow;	/* cb grows to whole file */
	uint64_t	rt_append;	/* cb offset for RL_APPEND */
	uint64_t	rt_violations;
	uint64_t	rt_slot;	/* fast request got a slot */
	uint64_t	rt_drained;	/* writer waited for a slot */
	uint64_t	rt_backout;	/* fast request in the tree */
	uint64_t	rt_nested;	/* nested, slots closed */
	uint64_t	rt_try_fail;	/* tryenter writer failed */
	uint64_t	rt_try_ok;	/* tryenter writer got it */
	uint64_t	rt_ops[NKIND];
} rl_test_t;

static rl_test_t rt;

#define	TRL	(&rt.rt_rl)

/* What zfs_rangelock_cb() does, reduced to what the lock sees. */
static void
rt_cb(zfs_locked_range_t *new, void *arg)
{
	(void) arg;

	if (new->lr_type == RL_APPEND) {
		new->lr_offset = rt.rt_append;
		new->lr_type = RL_WRITER;
	}
	if (rt.rt_grow) {
		new->lr_offset = 0;
		new->lr_length = UINT64_MAX;
	}
}

static void
rt_setup(zfs_rangelock_cb_t *cb)
{
	memset(&rt, 0, sizeof (rt));
	zfs_rangelock_init(TRL, cb, NULL);
	alarm(WATCHDOG_S);
}

static uint64_t *
rl_slots(void)
{
	return (atomic_load_ptr(&TRL->rl_slots));
}

static uint32_t
slots_used(void)
{
	uint64_t *slots = rl_slots();
	uint32_t n = 0;

	if (slots == NULL)
		return (0);
	for (int i = 0; i < RL_SLOTS; i++) {
		if (atomic_load_64(&slots[i]) != 0)
			n++;
	}
	return (n);
}

static uint32_t
rl_closed(void)
{
	return (atomic_load_32(&TRL->rl_closed));
}

static void
rt_teardown(void)
{
	unit_eq(slots_used(), 0);
	unit_eq(TRL->rl_writers, 0);
	unit_null(TRL->rl_drainer);
	unit_eq(avl_numnodes(&TRL->rl_tree), 0);
	zfs_rangelock_fini(TRL);
	alarm(0);
}

static uint32_t
rl_writers(void)
{
	mutex_enter(&TRL->rl_lock);
	uint32_t n = TRL->rl_writers;
	mutex_exit(&TRL->rl_lock);
	return (n);
}

static boolean_t
rl_draining(void)
{
	mutex_enter(&TRL->rl_lock);
	boolean_t d = (TRL->rl_drainer != NULL);
	mutex_exit(&TRL->rl_lock);
	return (d);
}

static boolean_t
write_wanted(zfs_locked_range_t *lr)
{
	mutex_enter(&TRL->rl_lock);
	boolean_t w = lr->lr_write_wanted;
	mutex_exit(&TRL->rl_lock);
	return (w);
}

/* A writer has marked the slot of the fast reader lr. */
static boolean_t
slot_wanted(zfs_locked_range_t *lr)
{
	uint64_t v = atomic_load_64(&rl_slots()[lr->lr_slot]);

	return ((v & RL_SLOT_WANTED) != 0);
}

#define	WAIT_UNTIL(cond) do {					\
	hrtime_t _end = gethrtime() + WAIT_NS;			\
	while (!(cond) && gethrtime() < _end)			\
		usleep(100);					\
	unit_true(cond);					\
} while (0)

static zfs_locked_range_t *
enter_fast(uint64_t off, uint64_t len, zfs_rangelock_type_t want)
{
	zfs_locked_range_t *lr;

	lr = zfs_rangelock_enter(TRL, off, len, RL_READER_FAST);
	unit_eq(lr->lr_type, want);
	return (lr);
}

static void
try_writer_fails(uint64_t off, uint64_t len)
{
	unit_null(zfs_rangelock_tryenter(TRL, off, len, RL_WRITER));
}

static void
try_writer_ok(uint64_t off, uint64_t len)
{
	zfs_locked_range_t *w;

	w = zfs_rangelock_tryenter(TRL, off, len, RL_WRITER);
	unit_notnull(w);
	unit_eq(rl_closed(), 1);
	zfs_rangelock_exit(w);
}

/*
 * A fast request that finds another reader in the tree allocates the
 * slots.  Do that on a new lock.
 */
static void
slots_on(void)
{
	zfs_locked_range_t *p;
	zfs_locked_range_t *f;

	unit_null(rl_slots());
	p = zfs_rangelock_enter(TRL, 0, PG, RL_READER);
	f = enter_fast(0, PG, RL_READER);
	zfs_rangelock_exit(f);
	zfs_rangelock_exit(p);
	unit_notnull(rl_slots());
	unit_eq(rl_closed(), 0);
}

/* ========== */

/* A thread that takes one lock, optionally after a reader. */
typedef struct locker {
	pthread_t	lk_tid;
	zfs_rangelock_type_t lk_type;
	uint64_t	lk_off;
	uint64_t	lk_len;
	boolean_t	lk_pre;		/* RL_READER at lk_pre_off */
	uint64_t	lk_pre_off;
	uint32_t	lk_pre_held;
	uint32_t	lk_held;
	uint32_t	lk_release;
} locker_t;

static void *
locker_thread(void *arg)
{
	locker_t *lk = arg;
	zfs_locked_range_t *pre = NULL;
	zfs_locked_range_t *lr;

	if (lk->lk_pre) {
		pre = zfs_rangelock_enter(TRL, lk->lk_pre_off,
		    lk->lk_len, RL_READER);
		atomic_swap_32(&lk->lk_pre_held, 1);
	}
	lr = zfs_rangelock_enter(TRL, lk->lk_off, lk->lk_len,
	    lk->lk_type);
	atomic_swap_32(&lk->lk_held, 1);
	while (atomic_load_32(&lk->lk_release) == 0)
		usleep(100);
	zfs_rangelock_exit(lr);
	if (pre != NULL)
		zfs_rangelock_exit(pre);
	return (NULL);
}

static void
locker_start(locker_t *lk, zfs_rangelock_type_t type, uint64_t off,
    uint64_t len)
{
	lk->lk_type = type;
	lk->lk_off = off;
	lk->lk_len = len;
	VERIFY0(pthread_create(&lk->lk_tid, NULL, locker_thread, lk));
}

static void
locker_stop(locker_t *lk)
{
	atomic_swap_32(&lk->lk_release, 1);
	VERIFY0(pthread_join(lk->lk_tid, NULL));
}

static boolean_t
lk_is_held(locker_t *lk)
{
	return (atomic_load_32(&lk->lk_held) != 0);
}

static void *
exit_thread(void *arg)
{
	zfs_rangelock_exit(arg);
	return (NULL);
}

/* ========== */

/*
 * The slots are allocated only when a fast request finds another
 * reader in the tree and no writer.
 */
static MunitResult
test_lazy_alloc(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	zfs_locked_range_t *p;
	zfs_locked_range_t *q;
	zfs_locked_range_t *w;
	zfs_locked_range_t *f;
	zfs_locked_range_t *g;
	zfs_locked_range_t *h;

	rt_setup(NULL);
	unit_eq(rl_closed(), 1);

	/* used like zv_rangelock and vre_rangelock */
	p = zfs_rangelock_enter(TRL, 0, 4 * PG, RL_READER);
	q = zfs_rangelock_enter(TRL, PG, 4 * PG, RL_READER);
	w = zfs_rangelock_enter(TRL, 8 * PG, PG, RL_WRITER);
	try_writer_fails(0, PG);
	zfs_rangelock_exit(w);
	zfs_rangelock_exit(q);
	zfs_rangelock_exit(p);
	unit_null(rl_slots());
	unit_eq(rl_closed(), 1);

	/* one fast reader at a time */
	for (int i = 0; i < 100; i++) {
		f = enter_fast(i * PG, PG, RL_READER);
		zfs_rangelock_exit(f);
	}
	unit_null(rl_slots());

	/* another reader, but also a writer */
	p = zfs_rangelock_enter(TRL, 0, PG, RL_READER);
	w = zfs_rangelock_enter(TRL, 8 * PG, PG, RL_WRITER);
	f = enter_fast(0, PG, RL_READER);
	unit_null(rl_slots());
	zfs_rangelock_exit(w);
	unit_eq(rl_closed(), 1);

	/* another reader and no writer */
	g = enter_fast(PG, PG, RL_READER);
	unit_notnull(rl_slots());
	unit_eq(rl_closed(), 0);
	unit_eq(slots_used(), 0);

	/* now fast requests stay out of the tree */
	ulong_t nodes = avl_numnodes(&TRL->rl_tree);
	q = enter_fast(0, PG, RL_READER_FAST);
	h = enter_fast(0, 2 * PG, RL_READER_FAST);
	unit_eq(avl_numnodes(&TRL->rl_tree), nodes);
	unit_eq(slots_used(), 2);
	zfs_rangelock_exit(q);
	zfs_rangelock_exit(h);
	zfs_rangelock_exit(g);
	zfs_rangelock_exit(f);
	zfs_rangelock_exit(p);

	rt_teardown();
	return (MUNIT_OK);
}

/* With no writer, fast readers share slots with tree readers. */
static MunitResult
test_slot_basic(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	zfs_locked_range_t *a;
	zfs_locked_range_t *b;
	zfs_locked_range_t *c;
	pthread_t tid;

	rt_setup(rt_cb);
	slots_on();

	a = enter_fast(0, 4 * PG, RL_READER_FAST);
	b = zfs_rangelock_tryenter(TRL, 2 * PG, 4 * PG,
	    RL_READER_FAST);
	unit_notnull(b);
	unit_eq(b->lr_type, RL_READER_FAST);
	unit_eq(slots_used(), 2);
	unit_eq(avl_numnodes(&TRL->rl_tree), 0);

	c = zfs_rangelock_enter(TRL, 0, 8 * PG, RL_READER);
	unit_eq(c->lr_type, RL_READER);
	unit_eq(avl_numnodes(&TRL->rl_tree), 1);
	unit_eq(slots_used(), 2);
	zfs_rangelock_exit(c);

	/* a fast reader can be released by another thread */
	VERIFY0(pthread_create(&tid, NULL, exit_thread, b));
	VERIFY0(pthread_join(tid, NULL));
	unit_eq(slots_used(), 1);
	zfs_rangelock_exit(a);

	rt_teardown();
	return (MUNIT_OK);
}

typedef struct racer {
	pthread_t	rc_tid;
	uint64_t	rc_off;
	uint32_t	*rc_go;
	zfs_locked_range_t *rc_lr;
	uint64_t	*rc_seen;
} racer_t;

static void *
racer_thread(void *arg)
{
	racer_t *rc = arg;

	while (atomic_load_32(rc->rc_go) == 0)
		;
	rc->rc_lr = zfs_rangelock_enter(TRL, rc->rc_off, PG,
	    RL_READER_FAST);
	rc->rc_seen = rl_slots();
	return (NULL);
}

/* Fast requests race to allocate the slots.  One array wins. */
static MunitResult
test_alloc_race(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	racer_t rc[NRACE];
	uint32_t go;
	uint64_t fell = 0;

	for (int iter = 0; iter < 200; iter++) {
		rt_setup(rt_cb);
		zfs_locked_range_t *p = zfs_rangelock_enter(TRL,
		    NPG * PG, PG, RL_READER);
		go = 0;
		for (int i = 0; i < NRACE; i++) {
			rc[i].rc_off = i * PG;
			rc[i].rc_go = &go;
			VERIFY0(pthread_create(&rc[i].rc_tid, NULL,
			    racer_thread, &rc[i]));
		}
		atomic_swap_32(&go, 1);
		for (int i = 0; i < NRACE; i++)
			VERIFY0(pthread_join(rc[i].rc_tid, NULL));
		unit_notnull(rl_slots());
		for (int i = 0; i < NRACE; i++) {
			munit_assert_ptr_equal(rc[i].rc_seen,
			    rl_slots());
			if (rc[i].rc_lr->lr_type == RL_READER)
				fell++;
			zfs_rangelock_exit(rc[i].rc_lr);
		}
		zfs_rangelock_exit(p);
		rt_teardown();
	}
	munit_logf(MUNIT_LOG_INFO, "ms=0 iters=200 tree_readers=%llu",
	    (u_longlong_t)fell);
	return (MUNIT_OK);
}

/* While a writer holds the lock, fast requests take the tree. */
static MunitResult
test_slot_backout(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	zfs_locked_range_t *w;
	zfs_locked_range_t *r;

	rt_setup(rt_cb);
	slots_on();

	w = zfs_rangelock_enter(TRL, 0, 4 * PG, RL_WRITER);
	unit_eq(rl_closed(), 1);
	unit_eq(TRL->rl_writers, 1);

	r = zfs_rangelock_tryenter(TRL, 8 * PG, 4 * PG,
	    RL_READER_FAST);
	unit_notnull(r);
	unit_eq(r->lr_type, RL_READER);
	unit_eq(slots_used(), 0);
	unit_eq(avl_numnodes(&TRL->rl_tree), 2);

	unit_null(zfs_rangelock_tryenter(TRL, 2 * PG, 4 * PG,
	    RL_READER_FAST));
	unit_null(zfs_rangelock_tryenter(TRL, 2 * PG, 4 * PG,
	    RL_READER));
	unit_eq(avl_numnodes(&TRL->rl_tree), 2);

	zfs_rangelock_exit(w);
	unit_eq(rl_closed(), 0);
	unit_eq(TRL->rl_writers, 0);

	w = enter_fast(2 * PG, 4 * PG, RL_READER_FAST);
	zfs_rangelock_exit(w);
	zfs_rangelock_exit(r);

	rt_teardown();
	return (MUNIT_OK);
}

/* A writer waits only for slots that overlap it. */
static MunitResult
test_writer_tryenter(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	zfs_locked_range_t *r;
	zfs_locked_range_t *p;
	locker_t lk = { 0 };

	rt_setup(rt_cb);
	slots_on();

	r = enter_fast(0, PG, RL_READER_FAST);
	try_writer_fails(0, PG);
	unit_eq(TRL->rl_writers, 0);
	unit_eq(rl_closed(), 0);
	try_writer_ok(PG, PG);
	unit_eq(rl_closed(), 0);

	/* the same with a writer waiting for r */
	locker_start(&lk, RL_WRITER, 0, 2 * PG);
	WAIT_UNTIL(rl_writers() == 1 && rl_draining());
	unit_true(slot_wanted(r));
	try_writer_fails(0, PG);
	unit_eq(rl_writers(), 1);
	unit_eq(rl_closed(), 1);
	try_writer_ok(4 * PG, PG);
	unit_eq(rl_writers(), 1);
	unit_false(lk_is_held(&lk));
	zfs_rangelock_exit(r);
	WAIT_UNTIL(lk_is_held(&lk));
	locker_stop(&lk);
	unit_eq(rl_closed(), 0);

	/* a failure in the tree reopens the slots too */
	p = zfs_rangelock_enter(TRL, 0, PG, RL_READER);
	try_writer_fails(0, PG);
	unit_eq(TRL->rl_writers, 0);
	unit_eq(rl_closed(), 0);
	zfs_rangelock_exit(p);

	rt_teardown();
	return (MUNIT_OK);
}

/* A slot covers its range rounded out to whole pages. */
static MunitResult
test_page_round(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	zfs_locked_range_t *r;

	rt_setup(rt_cb);
	slots_on();

	r = enter_fast(100, 100, RL_READER_FAST);
	try_writer_fails(3000, 100);
	try_writer_fails(150, 0);
	try_writer_ok(PG, 100);
	zfs_rangelock_exit(r);

	r = enter_fast(PG - 100, 200, RL_READER_FAST);
	try_writer_fails(0, 1);
	try_writer_fails(2 * PG - 1, 1);
	try_writer_ok(2 * PG, 1);
	zfs_rangelock_exit(r);

	/* a tree reader excludes only its bytes */
	r = zfs_rangelock_enter(TRL, 100, 100, RL_READER);
	try_writer_ok(3000, 100);
	zfs_rangelock_exit(r);

	rt_teardown();
	return (MUNIT_OK);
}

/* Ranges that do not pack take the tree; ends near 2^64. */
static MunitResult
test_slot_edges(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	const uint64_t top = 1ULL << (62 - RL_SLOT_LENBITS +
	    RL_SLOT_SHIFT);
	const uint64_t big = 1ULL << (RL_SLOT_LENBITS +
	    RL_SLOT_SHIFT);
	zfs_locked_range_t *r;

	rt_setup(rt_cb);
	slots_on();

	r = enter_fast(PG, 0, RL_READER);
	zfs_rangelock_exit(r);
	r = enter_fast(top, PG, RL_READER);
	zfs_rangelock_exit(r);
	r = enter_fast(0, big, RL_READER);
	zfs_rangelock_exit(r);
	r = enter_fast(5 * PG, UINT64_MAX, RL_READER);
	zfs_rangelock_exit(r);

	r = enter_fast(top - PG, PG, RL_READER_FAST);
	try_writer_fails(0, UINT64_MAX);
	try_writer_fails(top - 1, UINT64_MAX);
	try_writer_ok(top, UINT64_MAX);
	try_writer_ok(0, top - PG);
	zfs_rangelock_exit(r);

	r = enter_fast(0, big - PG, RL_READER_FAST);
	try_writer_fails(big - PG - 1, 1);
	try_writer_ok(big - PG, 1);
	zfs_rangelock_exit(r);

	rt_teardown();
	return (MUNIT_OK);
}

/* With every slot taken, fast requests take the tree. */
static MunitResult
test_slots_full(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	zfs_locked_range_t *r[RL_SLOTS];
	zfs_locked_range_t *f;

	rt_setup(rt_cb);
	slots_on();

	for (int i = 0; i < RL_SLOTS; i++)
		r[i] = enter_fast(i * PG, PG, RL_READER_FAST);
	unit_eq(slots_used(), RL_SLOTS);
	f = enter_fast(40 * PG, PG, RL_READER);
	unit_eq(avl_numnodes(&TRL->rl_tree), 1);
	try_writer_fails(7 * PG, PG);
	try_writer_fails(40 * PG, PG);
	try_writer_ok(50 * PG, PG);
	zfs_rangelock_exit(f);

	zfs_rangelock_exit(r[3]);
	f = enter_fast(41 * PG, PG, RL_READER_FAST);
	zfs_rangelock_exit(f);
	for (int i = 0; i < RL_SLOTS; i++) {
		if (i != 3)
			zfs_rangelock_exit(r[i]);
	}

	rt_teardown();
	return (MUNIT_OK);
}

/* The slot scan uses the range the callback leaves. */
static MunitResult
test_slot_callback(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	zfs_locked_range_t *r;
	zfs_locked_range_t *w;
	locker_t lk = { 0 };

	rt_setup(rt_cb);
	slots_on();

	r = enter_fast(40 * PG, PG, RL_READER_FAST);
	rt.rt_append = 40 * PG + 10;
	unit_null(zfs_rangelock_tryenter(TRL, 0, 10, RL_APPEND));
	rt.rt_append = 41 * PG;
	w = zfs_rangelock_tryenter(TRL, 0, 10, RL_APPEND);
	unit_notnull(w);
	unit_eq(w->lr_offset, 41 * PG);
	zfs_rangelock_exit(w);

	rt.rt_grow = B_TRUE;
	try_writer_fails(0, PG);
	locker_start(&lk, RL_WRITER, 0, PG);
	WAIT_UNTIL(rl_draining());
	unit_false(lk_is_held(&lk));
	zfs_rangelock_exit(r);
	WAIT_UNTIL(lk_is_held(&lk));
	locker_stop(&lk);
	rt.rt_grow = B_FALSE;

	rt_teardown();
	return (MUNIT_OK);
}

/*
 * A fast reader takes a nested reader while a writer of the same
 * range waits for its slot.  The nested reader must not wait, since
 * the writer is not in the tree yet.
 */
static MunitResult
test_nested_drain(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	zfs_locked_range_t *r;
	zfs_locked_range_t *n;
	locker_t lk = { 0 };

	rt_setup(rt_cb);
	slots_on();

	r = enter_fast(0, 4 * PG, RL_READER_FAST);
	locker_start(&lk, RL_WRITER, 0, 4 * PG);
	WAIT_UNTIL(rl_writers() == 1 && rl_draining());

	n = zfs_rangelock_enter(TRL, 2 * PG, 4 * PG, RL_READER);
	unit_eq(n->lr_type, RL_READER);
	zfs_rangelock_exit(n);
	n = enter_fast(2 * PG, 4 * PG, RL_READER);
	zfs_rangelock_exit(n);
	unit_false(lk_is_held(&lk));

	zfs_rangelock_exit(r);
	WAIT_UNTIL(lk_is_held(&lk));
	locker_stop(&lk);

	rt_teardown();
	return (MUNIT_OK);
}

/*
 * Several writers wait on one drainer's cv, one of them holding a
 * reader that the drainer needs.  All of them must get past the
 * slots together when the slot clears.
 */
static MunitResult
test_drain_shared_cv(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	zfs_locked_range_t *r;
	locker_t w1 = { 0 };
	locker_t w2 = { 0 };
	locker_t w3 = { 0 };
	locker_t d = { 0 };

	rt_setup(rt_cb);
	slots_on();

	r = enter_fast(0, NPG * PG, RL_READER_FAST);

	locker_start(&w1, RL_WRITER, 0, PG);
	WAIT_UNTIL(rl_writers() == 1 && rl_draining());

	/* dedupe: a reader, then a writer, in one thread */
	d.lk_pre = B_TRUE;
	d.lk_pre_off = 0;
	locker_start(&d, RL_WRITER, 32 * PG, PG);
	WAIT_UNTIL(rl_writers() == 2);
	unit_eq(atomic_load_32(&d.lk_pre_held), 1);

	locker_start(&w2, RL_WRITER, 16 * PG, PG);
	locker_start(&w3, RL_WRITER, 24 * PG, PG);
	WAIT_UNTIL(rl_writers() == 4);

	zfs_rangelock_exit(r);
	WAIT_UNTIL(lk_is_held(&d) && lk_is_held(&w2) &&
	    lk_is_held(&w3));
	WAIT_UNTIL(!rl_draining());
	unit_false(lk_is_held(&w1));

	locker_stop(&d);
	WAIT_UNTIL(lk_is_held(&w1));
	locker_stop(&w1);
	locker_stop(&w2);
	locker_stop(&w3);

	rt_teardown();
	return (MUNIT_OK);
}

/*
 * w2 sleeps on the cv of the drainer w1, for a slot w1 does not
 * overlap.  w2 must still get in when its slot clears while w1 holds
 * its range.
 */
static MunitResult
test_drainer_handoff(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	zfs_locked_range_t *ra;
	zfs_locked_range_t *rb;
	locker_t w1 = { 0 };
	locker_t w2 = { 0 };

	rt_setup(rt_cb);
	slots_on();

	ra = enter_fast(0, PG, RL_READER_FAST);
	rb = enter_fast(8 * PG, PG, RL_READER_FAST);
	locker_start(&w1, RL_WRITER, 0, PG);
	WAIT_UNTIL(rl_writers() == 1 && rl_draining());
	locker_start(&w2, RL_WRITER, 8 * PG, PG);
	WAIT_UNTIL(rl_writers() == 2 && slot_wanted(rb));

	zfs_rangelock_exit(ra);
	WAIT_UNTIL(lk_is_held(&w1));
	unit_false(lk_is_held(&w2));
	zfs_rangelock_exit(rb);
	WAIT_UNTIL(lk_is_held(&w2));
	locker_stop(&w2);
	locker_stop(&w1);

	rt_teardown();
	return (MUNIT_OK);
}

/*
 * A writer waits for a slot, then for a tree reader, and then finds
 * a slot again, as it does when a reader backs out of one.  It must
 * become the drainer a second time.
 */
static MunitResult
test_drainer_twice(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	zfs_locked_range_t *t;
	zfs_locked_range_t *r;
	locker_t lk = { 0 };
	uint64_t *slots;

	rt_setup(rt_cb);
	slots_on();
	slots = rl_slots();

	t = zfs_rangelock_enter(TRL, 0, PG, RL_READER);
	r = enter_fast(0, PG, RL_READER_FAST);
	locker_start(&lk, RL_WRITER, 0, PG);
	WAIT_UNTIL(rl_writers() == 1 && rl_draining());
	zfs_rangelock_exit(r);
	WAIT_UNTIL(!rl_draining() && write_wanted(t));

	/* a reader that will see rl_closed holds slot 0 */
	unit_eq(atomic_cas_64(&slots[0], 0, 1), 0);
	zfs_rangelock_exit(t);
	WAIT_UNTIL(rl_draining());
	unit_false(lk_is_held(&lk));

	/* it backs out */
	unit_true(atomic_swap_64(&slots[0], 0) & RL_SLOT_WANTED);
	mutex_enter(&TRL->rl_lock);
	cv_broadcast(&TRL->rl_drainer->lr_write_cv);
	mutex_exit(&TRL->rl_lock);
	WAIT_UNTIL(lk_is_held(&lk));
	locker_stop(&lk);

	rt_teardown();
	return (MUNIT_OK);
}

/* ========== */

static uint64_t
xs(uint64_t *s)
{
	uint64_t x = *s;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	return (*s = x);
}

static void
spin(uint64_t *s, int max)
{
	volatile int n = (int)(xs(s) % (uint64_t)max);

	while (n > 0)
		n--;
}

static void
violation(void)
{
	atomic_inc_64(&rt.rt_violations);
}

/* Pages [*first, *end) that bytes [off, off + len) touch. */
static void
pages(uint64_t off, uint64_t len, uint64_t *first, uint64_t *end)
{
	if (len >= NPG * PG - off)
		len = NPG * PG - off;
	*first = off / PG;
	*end = (off + len + PG - 1) / PG;
}

static void
check_in(uint64_t off, uint64_t len, boolean_t wr)
{
	uint64_t b0, b1;

	pages(off, len, &b0, &b1);
	if (!wr) {
		for (uint64_t b = b0; b < b1; b++)
			atomic_inc_32(&rt.rt_rd[b]);
		for (uint64_t b = b0; b < b1; b++) {
			if (atomic_load_32(&rt.rt_wr[b]) != 0)
				violation();
		}
	} else {
		for (uint64_t b = b0; b < b1; b++)
			atomic_inc_32(&rt.rt_wr[b]);
		for (uint64_t b = b0; b < b1; b++) {
			if (atomic_load_32(&rt.rt_wr[b]) != 1 ||
			    atomic_load_32(&rt.rt_rd[b]) != 0)
				violation();
		}
	}
}

static void
check_out(uint64_t off, uint64_t len, boolean_t wr)
{
	uint64_t b0, b1;

	pages(off, len, &b0, &b1);
	for (uint64_t b = b0; b < b1; b++)
		atomic_dec_32(wr ? &rt.rt_wr[b] : &rt.rt_rd[b]);
}

static boolean_t
is_fast(zfs_locked_range_t *lr)
{
	return (lr->lr_type == RL_READER_FAST);
}

static zfs_locked_range_t *
reader_in(zfs_locked_range_t *lr, uint64_t off, uint64_t len,
    zfs_rangelock_type_t type)
{
	if (is_fast(lr))
		atomic_inc_64(&rt.rt_slot);
	else if (type == RL_READER_FAST)
		atomic_inc_64(&rt.rt_backout);
	check_in(off, len, B_FALSE);
	return (lr);
}

static void
reader_out(zfs_locked_range_t *lr, uint64_t off, uint64_t len)
{
	if (is_fast(lr) && slot_wanted(lr))
		atomic_inc_64(&rt.rt_drained);
	check_out(off, len, B_FALSE);
	zfs_rangelock_exit(lr);
}

static zfs_locked_range_t *
reader_enter(uint64_t off, uint64_t len, zfs_rangelock_type_t type)
{
	zfs_locked_range_t *lr;

	lr = zfs_rangelock_enter(TRL, off, len, type);
	return (reader_in(lr, off, len, type));
}

typedef struct worker {
	pthread_t	wk_tid;
	int		wk_kind;
	uint64_t	wk_seed;
	uint64_t	wk_ops;
	hrtime_t	wk_maxwait;
	uint32_t	wk_done;
} worker_t;

/* Bytes that start in page pg and end in page pg + npg - 1. */
static void
rand_bytes(worker_t *wk, uint64_t pg, uint64_t npg, uint64_t *off,
    uint64_t *len)
{
	uint64_t o = pg * PG + xs(&wk->wk_seed) % PG;
	uint64_t e = (pg + npg - 1) * PG + 1 + xs(&wk->wk_seed) % PG;

	if (e <= o)
		e = o + 1;
	*off = o;
	*len = e - o;
}

static void
do_reader(worker_t *wk, uint64_t pg, uint64_t npg,
    zfs_rangelock_type_t type)
{
	uint64_t off, len;
	zfs_locked_range_t *lr;

	rand_bytes(wk, pg, npg, &off, &len);
	lr = reader_enter(off, len, type);
	spin(&wk->wk_seed, 200);
	reader_out(lr, off, len);
}

static void
do_writer(worker_t *wk, uint64_t pg, uint64_t npg)
{
	uint64_t off = pg * PG;
	uint64_t len = npg * PG;
	zfs_locked_range_t *lr;

	if (rt.rt_whole && xs(&wk->wk_seed) % 64 == 0) {
		off = 0;
		len = UINT64_MAX;
	}
	hrtime_t t0 = gethrtime();
	lr = zfs_rangelock_enter(TRL, off, len, RL_WRITER);
	hrtime_t w = gethrtime() - t0;
	if (w > wk->wk_maxwait)
		wk->wk_maxwait = w;
	check_in(off, len, B_TRUE);
	spin(&wk->wk_seed, 200);
	check_out(off, len, B_TRUE);
	zfs_rangelock_exit(lr);
	usleep(xs(&wk->wk_seed) % 200);
}

static void
do_try(worker_t *wk, uint64_t pg, uint64_t npg)
{
	static const zfs_rangelock_type_t types[] = {
		RL_READER_FAST, RL_READER, RL_WRITER
	};
	zfs_rangelock_type_t type = types[xs(&wk->wk_seed) % 3];
	uint64_t off = pg * PG;
	uint64_t len = npg * PG;
	zfs_locked_range_t *lr;

	if (type != RL_WRITER)
		rand_bytes(wk, pg, npg, &off, &len);
	lr = zfs_rangelock_tryenter(TRL, off, len, type);
	if (type == RL_WRITER) {
		if (lr == NULL) {
			atomic_inc_64(&rt.rt_try_fail);
			return;
		}
		atomic_inc_64(&rt.rt_try_ok);
		check_in(off, len, B_TRUE);
		check_out(off, len, B_TRUE);
		zfs_rangelock_exit(lr);
	} else if (lr != NULL) {
		reader_in(lr, off, len, type);
		reader_out(lr, off, len);
	}
}

/* Lock order as in zfs_dedupe_range() within one file. */
static void
do_dedupe(uint64_t pg, uint64_t npg)
{
	uint64_t off = pg * PG;
	uint64_t o2 = ((pg + 8) % (NPG - 4)) * PG;
	uint64_t len = npg * PG;
	zfs_locked_range_t *r;
	zfs_locked_range_t *w;

	if (off < o2 + len && o2 < off + len)
		return;
	if (off < o2) {
		r = reader_enter(off, len, RL_READER);
		w = zfs_rangelock_enter(TRL, o2, len, RL_WRITER);
		check_in(o2, len, B_TRUE);
	} else {
		w = zfs_rangelock_enter(TRL, o2, len, RL_WRITER);
		check_in(o2, len, B_TRUE);
		r = reader_enter(off, len, RL_READER);
	}
	check_out(o2, len, B_TRUE);
	zfs_rangelock_exit(w);
	reader_out(r, off, len);
}

static void
do_nested(worker_t *wk, uint64_t pg, uint64_t npg)
{
	uint64_t off, len, o2, l2;
	zfs_locked_range_t *r;
	zfs_locked_range_t *n;

	rand_bytes(wk, pg, npg, &off, &len);
	r = reader_enter(off, len, RL_READER_FAST);
	if (!is_fast(r)) {
		/* only nesting under a slot is new */
		reader_out(r, off, len);
		return;
	}
	rand_bytes(wk, xs(&wk->wk_seed) % NPG, 1, &o2, &l2);
	spin(&wk->wk_seed, 400);
	boolean_t closed = (rl_closed() != 0);
	n = reader_enter(o2, l2, (xs(&wk->wk_seed) & 1) ?
	    RL_READER_FAST : RL_READER);
	if (closed)
		atomic_inc_64(&rt.rt_nested);
	reader_out(n, o2, l2);
	reader_out(r, off, len);
}

static void *
worker_thread(void *arg)
{
	worker_t *wk = arg;

	while (atomic_load_32(&rt.rt_stop) == 0) {
		uint64_t pg = xs(&wk->wk_seed) % NPG;
		uint64_t npg = 1 + xs(&wk->wk_seed) % 4;
		if (pg + npg > NPG)
			npg = NPG - pg;

		switch (wk->wk_kind) {
		case K_FAST:
			do_reader(wk, pg, npg, RL_READER_FAST);
			break;
		case K_TREE:
			do_reader(wk, pg, npg, RL_READER);
			break;
		case K_WRITER:
			do_writer(wk, pg, npg);
			break;
		case K_TRY:
			do_try(wk, pg, npg);
			break;
		case K_DEDUPE:
			do_dedupe(pg, npg);
			break;
		case K_NESTED:
			do_nested(wk, pg, npg);
			break;
		}
		atomic_inc_64(&rt.rt_ops[wk->wk_kind]);
		atomic_inc_64(&wk->wk_ops);
	}
	atomic_swap_32(&wk->wk_done, 1);
	return (NULL);
}

typedef struct need {
	const char	*nd_name;
	uint64_t	*nd_ctr;
	uint64_t	nd_min;
} need_t;

static boolean_t
needs_met(const need_t *need, int nneed)
{
	for (int i = 0; i < nneed; i++) {
		if (atomic_load_64(need[i].nd_ctr) < need[i].nd_min)
			return (B_FALSE);
	}
	return (B_TRUE);
}

static int
start_workers(worker_t *wk, const int counts[NKIND], hrtime_t start)
{
	int n = 0;

	for (int k = 0; k < NKIND; k++) {
		for (int i = 0; i < counts[k]; i++, n++) {
			VERIFY3S(n, <, MAXTHR);
			memset(&wk[n], 0, sizeof (wk[n]));
			wk[n].wk_kind = k;
			wk[n].wk_seed = 0x9e3779b97f4a7c15ULL *
			    (uint64_t)(n + 1);
			wk[n].wk_seed ^= (uint64_t)start;
			wk[n].wk_seed |= 1;
			VERIFY0(pthread_create(&wk[n].wk_tid, NULL,
			    worker_thread, &wk[n]));
		}
	}
	return (n);
}

/* Fail if a thread has made no progress for STALL_NS. */
static void
check_progress(worker_t *wk, int n, uint64_t *last,
    hrtime_t *lastp, hrtime_t now)
{
	for (int i = 0; i < n; i++) {
		uint64_t ops = atomic_load_64(&wk[i].wk_ops);
		if (ops != last[i]) {
			last[i] = ops;
			lastp[i] = now;
			continue;
		}
		if (now - lastp[i] > STALL_NS) {
			munit_errorf("%s thread %d stalled, "
			    "rl_closed %u slots %u",
			    kind_name[wk[i].wk_kind], i, rl_closed(),
			    slots_used());
		}
	}
}

/* Stop the workers.  One that has not finished in STALL_NS fails. */
static void
stop_workers(worker_t *wk, int n)
{
	hrtime_t end;

	atomic_swap_32(&rt.rt_stop, 1);
	end = gethrtime() + STALL_NS;
	for (int i = 0; i < n; i++) {
		while (atomic_load_32(&wk[i].wk_done) == 0) {
			if (gethrtime() > end) {
				munit_errorf("%s thread %d stuck, "
				    "rl_closed %u slots %u",
				    kind_name[wk[i].wk_kind], i,
				    rl_closed(), slots_used());
			}
			usleep(1000);
		}
		VERIFY0(pthread_join(wk[i].wk_tid, NULL));
	}
}

/*
 * Run counts[k] threads of each kind until every need is met, or
 * fail.  A thread that makes no progress for STALL_NS fails too.
 */
static void
run_mix(const int counts[NKIND], boolean_t whole, const need_t *need,
    int nneed)
{
	worker_t wk[MAXTHR];
	uint64_t last[MAXTHR] = { 0 };
	hrtime_t lastp[MAXTHR];
	hrtime_t start = gethrtime();
	hrtime_t now;
	hrtime_t wmax = 0;
	int n;

	rt.rt_whole = whole;
	n = start_workers(wk, counts, start);
	for (int i = 0; i < n; i++)
		lastp[i] = start;
	for (;;) {
		usleep(10000);
		now = gethrtime();
		check_progress(wk, n, last, lastp, now);
		if (now - start >= MAX_RUN_NS ||
		    atomic_load_64(&rt.rt_violations) != 0)
			break;
		if (now - start >= MIN_RUN_NS &&
		    needs_met(need, nneed))
			break;
	}
	stop_workers(wk, n);
	for (int i = 0; i < n; i++) {
		if (wk[i].wk_maxwait > wmax)
			wmax = wk[i].wk_maxwait;
	}

	munit_logf(MUNIT_LOG_INFO, "ms=%llu fast=%llu tree=%llu "
	    "writer=%llu try=%llu dedupe=%llu nested_ops=%llu "
	    "slot=%llu drained=%llu backout=%llu nested=%llu "
	    "try_fail=%llu try_ok=%llu writer_maxwait_us=%llu "
	    "violations=%llu",
	    (u_longlong_t)NSEC2MSEC(now - start),
	    (u_longlong_t)rt.rt_ops[K_FAST],
	    (u_longlong_t)rt.rt_ops[K_TREE],
	    (u_longlong_t)rt.rt_ops[K_WRITER],
	    (u_longlong_t)rt.rt_ops[K_TRY],
	    (u_longlong_t)rt.rt_ops[K_DEDUPE],
	    (u_longlong_t)rt.rt_ops[K_NESTED],
	    (u_longlong_t)rt.rt_slot, (u_longlong_t)rt.rt_drained,
	    (u_longlong_t)rt.rt_backout, (u_longlong_t)rt.rt_nested,
	    (u_longlong_t)rt.rt_try_fail, (u_longlong_t)rt.rt_try_ok,
	    (u_longlong_t)NSEC2USEC(wmax),
	    (u_longlong_t)rt.rt_violations);

	unit_zero(rt.rt_violations);
	for (int i = 0; i < nneed; i++) {
		uint64_t got = *need[i].nd_ctr;
		if (got < need[i].nd_min) {
			munit_errorf("%s: %llu, needed %llu",
			    need[i].nd_name, (u_longlong_t)got,
			    (u_longlong_t)need[i].nd_min);
		}
	}
}

#define	NEED(ctr, min)	{ #ctr, &rt.ctr, (min) }

/* Fast readers against writers, some of them whole-file. */
static MunitResult
test_mix_fast_writers(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	static const int counts[NKIND] = { 4, 1, 2, 0, 0, 0 };
	const need_t need[] = {
		NEED(rt_slot, 1),
		NEED(rt_drained, 1),
		NEED(rt_backout, 1),
		NEED(rt_ops[K_WRITER], 1),
	};

	rt_setup(rt_cb);
	run_mix(counts, B_TRUE, need, ARRAY_SIZE(need));
	rt_teardown();
	return (MUNIT_OK);
}

/* A writer keeps getting through constant fast readers. */
static MunitResult
test_mix_drain(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	static const int counts[NKIND] = { 6, 0, 1, 0, 0, 0 };
	const need_t need[] = {
		NEED(rt_drained, 1),
		NEED(rt_ops[K_WRITER], 100),
	};

	rt_setup(rt_cb);
	run_mix(counts, B_FALSE, need, ARRAY_SIZE(need));
	rt_teardown();
	return (MUNIT_OK);
}

/* tryenter of every type against fast readers and a writer. */
static MunitResult
test_mix_tryenter(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	static const int counts[NKIND] = { 2, 0, 1, 2, 0, 0 };
	const need_t need[] = {
		NEED(rt_slot, 1),
		NEED(rt_try_fail, 1),
		NEED(rt_try_ok, 1),
		NEED(rt_backout, 1),
	};

	rt_setup(rt_cb);
	run_mix(counts, B_FALSE, need, ARRAY_SIZE(need));
	rt_teardown();
	return (MUNIT_OK);
}

/* A reader and a writer in one thread, against fast readers. */
static MunitResult
test_mix_dedupe(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	static const int counts[NKIND] = { 4, 0, 0, 0, 3, 0 };
	const need_t need[] = {
		NEED(rt_drained, 1),
		/*
		 * Not a race requirement but a minimum amount of
		 * work, so that a drainer that forgets to wake the
		 * writers sharing its cv hangs this test most runs.
		 */
		NEED(rt_ops[K_DEDUPE], 100000),
	};

	rt_setup(rt_cb);
	run_mix(counts, B_FALSE, need, ARRAY_SIZE(need));
	rt_teardown();
	return (MUNIT_OK);
}

/* Fast readers that nest a reader while writers wait. */
static MunitResult
test_mix_nested(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	static const int counts[NKIND] = { 2, 0, 2, 0, 0, 3 };
	const need_t need[] = {
		NEED(rt_nested, 1),
		NEED(rt_drained, 1),
	};

	rt_setup(rt_cb);
	run_mix(counts, B_TRUE, need, ARRAY_SIZE(need));
	rt_teardown();
	return (MUNIT_OK);
}

/* ========== */

static const MunitTest rangelock_tests[] = {
	UNIT_TEST("lazy_alloc",		test_lazy_alloc),
	UNIT_TEST("slot_basic",		test_slot_basic),
	UNIT_TEST("alloc_race",		test_alloc_race),
	UNIT_TEST("slot_backout",	test_slot_backout),
	UNIT_TEST("writer_tryenter",	test_writer_tryenter),
	UNIT_TEST("page_round",		test_page_round),
	UNIT_TEST("slot_edges",		test_slot_edges),
	UNIT_TEST("slots_full",		test_slots_full),
	UNIT_TEST("slot_callback",	test_slot_callback),
	UNIT_TEST("nested_drain",	test_nested_drain),
	UNIT_TEST("drain_shared_cv",	test_drain_shared_cv),
	UNIT_TEST("drainer_handoff",	test_drainer_handoff),
	UNIT_TEST("drainer_twice",	test_drainer_twice),
	UNIT_TEST("mix_fast_writers",	test_mix_fast_writers),
	UNIT_TEST("mix_drain",		test_mix_drain),
	UNIT_TEST("mix_tryenter",	test_mix_tryenter),
	UNIT_TEST("mix_dedupe",		test_mix_dedupe),
	UNIT_TEST("mix_nested",		test_mix_nested),
	{ 0 },
};

static const MunitSuite rangelock_test_suite = {
	"rangelock.",
	rangelock_tests,
	NULL,
	1,
	MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	return (munit_suite_main(&rangelock_test_suite, NULL, argc,
	    argv));
}
