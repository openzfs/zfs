/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2017, 2018 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/aggsum.h>

/*
 * Aggregate-sum counters are a form of fanned-out counter, used when atomic
 * instructions on a single field cause enough CPU cache line contention to
 * slow system performance. Due to their increased overhead and the expense
 * involved with precisely reading from them, they should only be used in cases
 * where the write rate (increment/decrement) is much higher than the read rate
 * (get value).
 *
 * Aggregate sum counters are comprised of two basic parts, the core and the
 * buckets. The core counter contains a lock for the entire counter, as well
 * as the current upper and lower bounds on the value of the counter. The
 * aggsum_bucket structure contains a per-bucket lock to protect the contents of
 * the bucket, the current amount that this bucket has changed from the global
 * counter (called the delta), and the amount of increment and decrement we have
 * "borrowed" from the core counter.
 *
 * The basic operation of an aggsum is simple. Threads that wish to modify the
 * counter will modify one bucket's counter (determined by their current CPU, to
 * help minimize lock and cache contention). If the bucket already has
 * sufficient capacity borrowed from the core structure to handle their request,
 * they simply modify the delta and return.  If the bucket does not, we clear
 * the bucket's current state (to prevent the borrowed amounts from getting too
 * large), and borrow more from the core counter. Borrowing is done by adding to
 * the upper bound (or subtracting from the lower bound) of the core counter,
 * and setting the borrow value for the bucket to the amount added (or
 * subtracted).  Clearing the bucket is the opposite; we add the current delta
 * to both the lower and upper bounds of the core counter, subtract the borrowed
 * incremental from the upper bound, and add the borrowed decrement from the
 * lower bound.  Note that only borrowing and clearing require access to the
 * core counter; since all other operations access CPU-local resources,
 * performance can be much higher than a traditional counter.
 *
 * Threads that wish to read from the counter have a slightly more challenging
 * task. It is fast to determine the upper and lower bounds of the aggum; this
 * does not require grabbing any locks. This suffices for cases where an
 * approximation of the aggsum's value is acceptable. However, if one needs to
 * know whether some specific value is above or below the current value in the
 * aggsum, they invoke aggsum_compare(). This function operates by repeatedly
 * comparing the target value to the upper and lower bounds of the aggsum, and
 * then clearing a bucket. This proceeds until the target is outside of the
 * upper and lower bounds and we return a response, or the last bucket has been
 * cleared and we know that the target is equal to the aggsum's value. Finally,
 * the most expensive operation is determining the precise value of the aggsum.
 * To do this, we clear every bucket and then return the upper bound (which must
 * be equal to the lower bound). What makes aggsum_compare() and aggsum_value()
 * expensive is clearing buckets. This involves grabbing the global lock
 * (serializing against themselves and borrow operations), grabbing a bucket's
 * lock (preventing threads on those CPUs from modifying their delta), and
 * zeroing out the borrowed value (forcing that thread to borrow on its next
 * request, which will also be expensive).  This is what makes aggsums well
 * suited for write-many read-rarely operations.
 *
 * Note that the aggsums do not expand if more CPUs are hot-added. In that
 * case, we will have less fanout than boot_ncpus, but we don't want to always
 * reserve the RAM necessary to create the extra slots for additional CPUs up
 * front, and dynamically adding them is a complex task.
 */

/*
 * We will borrow 2^aggsum_borrow_shift times the current request, so we will
 * have to get the as_lock approximately every 2^aggsum_borrow_shift calls to
 * aggsum_add().
 */
static uint_t aggsum_borrow_shift = 4;

void
aggsum_init(aggsum_t *as, uint64_t value)
{
	bzero(as, sizeof (*as));
	as->as_lower_bound = as->as_upper_bound = value;
	mutex_init(&as->as_lock, NULL, MUTEX_DEFAULT, NULL);
	/*
	 * Too many buckets may hurt read performance without improving
	 * write.  From 12 CPUs use bucket per 2 CPUs, from 48 per 4, etc.
	 */
	as->as_bucketshift = highbit64(boot_ncpus / 6) / 2;
	as->as_numbuckets = ((boot_ncpus - 1) >> as->as_bucketshift) + 1;
	as->as_buckets = kmem_zalloc(as->as_numbuckets *
	    sizeof (aggsum_bucket_t), KM_SLEEP);
	for (int i = 0; i < as->as_numbuckets; i++) {
		mutex_init(&as->as_buckets[i].asc_lock,
		    NULL, MUTEX_DEFAULT, NULL);
	}
}

void
aggsum_fini(aggsum_t *as)
{
	for (int i = 0; i < as->as_numbuckets; i++)
		mutex_destroy(&as->as_buckets[i].asc_lock);
	kmem_free(as->as_buckets, as->as_numbuckets * sizeof (aggsum_bucket_t));
	mutex_destroy(&as->as_lock);
}

int64_t
aggsum_lower_bound(aggsum_t *as)
{
	return (atomic_load_64((volatile uint64_t *)&as->as_lower_bound));
}

uint64_t
aggsum_upper_bound(aggsum_t *as)
{
	return (atomic_load_64(&as->as_upper_bound));
}

uint64_t
aggsum_value(aggsum_t *as)
{
	int64_t lb;
	uint64_t ub;

	mutex_enter(&as->as_lock);
	lb = as->as_lower_bound;
	ub = as->as_upper_bound;
	if (lb == ub) {
		for (int i = 0; i < as->as_numbuckets; i++) {
			ASSERT0(as->as_buckets[i].asc_delta);
			ASSERT0(as->as_buckets[i].asc_borrowed);
		}
		mutex_exit(&as->as_lock);
		return (lb);
	}
	for (int i = 0; i < as->as_numbuckets; i++) {
		struct aggsum_bucket *asb = &as->as_buckets[i];
		if (asb->asc_borrowed == 0)
			continue;
		mutex_enter(&asb->asc_lock);
		lb += asb->asc_delta + asb->asc_borrowed;
		ub += asb->asc_delta - asb->asc_borrowed;
		asb->asc_delta = 0;
		asb->asc_borrowed = 0;
		mutex_exit(&asb->asc_lock);
	}
	ASSERT3U(lb, ==, ub);
	atomic_store_64((volatile uint64_t *)&as->as_lower_bound, lb);
	atomic_store_64(&as->as_upper_bound, lb);
	mutex_exit(&as->as_lock);

	return (lb);
}

void
aggsum_add(aggsum_t *as, int64_t delta)
{
	struct aggsum_bucket *asb;
	int64_t borrow;

	asb = &as->as_buckets[(CPU_SEQID_UNSTABLE >> as->as_bucketshift) %
	    as->as_numbuckets];

	/* Try fast path if we already borrowed enough before. */
	mutex_enter(&asb->asc_lock);
	if (asb->asc_delta + delta <= (int64_t)asb->asc_borrowed &&
	    asb->asc_delta + delta >= -(int64_t)asb->asc_borrowed) {
		asb->asc_delta += delta;
		mutex_exit(&asb->asc_lock);
		return;
	}
	mutex_exit(&asb->asc_lock);

	/*
	 * We haven't borrowed enough.  Take the global lock and borrow
	 * considering what is requested now and what we borrowed before.
	 */
	borrow = (delta < 0 ? -delta : delta);
	borrow <<= aggsum_borrow_shift + as->as_bucketshift;
	mutex_enter(&as->as_lock);
	if (borrow >= asb->asc_borrowed)
		borrow -= asb->asc_borrowed;
	else
		borrow = (borrow - (int64_t)asb->asc_borrowed) / 4;
	mutex_enter(&asb->asc_lock);
	delta += asb->asc_delta;
	asb->asc_delta = 0;
	asb->asc_borrowed += borrow;
	mutex_exit(&asb->asc_lock);
	atomic_store_64((volatile uint64_t *)&as->as_lower_bound,
	    as->as_lower_bound + delta - borrow);
	atomic_store_64(&as->as_upper_bound,
	    as->as_upper_bound + delta + borrow);
	mutex_exit(&as->as_lock);
}

/*
 * Compare the aggsum value to target efficiently. Returns -1 if the value
 * represented by the aggsum is less than target, 1 if it's greater, and 0 if
 * they are equal.
 */
int
aggsum_compare(aggsum_t *as, uint64_t target)
{
	int64_t lb;
	uint64_t ub;
	int i;

	if (atomic_load_64(&as->as_upper_bound) < target)
		return (-1);
	lb = atomic_load_64((volatile uint64_t *)&as->as_lower_bound);
	if (lb > 0 && (uint64_t)lb > target)
		return (1);
	mutex_enter(&as->as_lock);
	lb = as->as_lower_bound;
	ub = as->as_upper_bound;
	for (i = 0; i < as->as_numbuckets; i++) {
		struct aggsum_bucket *asb = &as->as_buckets[i];
		if (asb->asc_borrowed == 0)
			continue;
		mutex_enter(&asb->asc_lock);
		lb += asb->asc_delta + asb->asc_borrowed;
		ub += asb->asc_delta - asb->asc_borrowed;
		asb->asc_delta = 0;
		asb->asc_borrowed = 0;
		mutex_exit(&asb->asc_lock);
		if (ub < target || (lb > 0 && (uint64_t)lb > target))
			break;
	}
	if (i >= as->as_numbuckets)
		ASSERT3U(lb, ==, ub);
	atomic_store_64((volatile uint64_t *)&as->as_lower_bound, lb);
	atomic_store_64(&as->as_upper_bound, ub);
	mutex_exit(&as->as_lock);
	return (ub < target ? -1 : (uint64_t)lb > target ? 1 : 0);
}
