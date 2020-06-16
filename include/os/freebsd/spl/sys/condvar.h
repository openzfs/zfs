/*
 * Copyright (c) 2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * Copyright (c) 2013 iXsystems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _OPENSOLARIS_SYS_CONDVAR_H_
#define	_OPENSOLARIS_SYS_CONDVAR_H_

#include <sys/param.h>

#include <sys/spl_condvar.h>
#include <sys/mutex.h>
#include <sys/time.h>
#include <sys/kmem.h>

static __inline sbintime_t
zfs_nstosbt(int64_t _ns)
{
	sbintime_t sb = 0;

#ifdef KASSERT
	KASSERT(_ns >= 0, ("Negative values illegal for nstosbt: %jd", _ns));
#endif
	if (_ns >= SBT_1S) {
		sb = (_ns / 1000000000) * SBT_1S;
		_ns = _ns % 1000000000;
	}
	/* 9223372037 = ceil(2^63 / 1000000000) */
	sb += ((_ns * 9223372037ull) + 0x7fffffff) >> 31;
	return (sb);
}


typedef struct cv	kcondvar_t;
#define	CALLOUT_FLAG_ABSOLUTE C_ABSOLUTE

typedef enum {
	CV_DEFAULT,
	CV_DRIVER
} kcv_type_t;

#define	zfs_cv_init(cv, name, type, arg)	do {			\
	const char *_name;						\
	ASSERT((type) == CV_DEFAULT);					\
	for (_name = #cv; *_name != '\0'; _name++) {			\
		if (*_name >= 'a' && *_name <= 'z')			\
			break;						\
	}								\
	if (*_name == '\0')						\
		_name = #cv;						\
	cv_init((cv), _name);						\
} while (0)
#define	cv_init(cv, name, type, arg)	zfs_cv_init(cv, name, type, arg)


static inline int
cv_wait_sig(kcondvar_t *cvp, kmutex_t *mp)
{

	return (_cv_wait_sig(cvp, &(mp)->lock_object) == 0);
}

static inline int
cv_timedwait(kcondvar_t *cvp, kmutex_t *mp, clock_t timo)
{
	int rc;

	timo -= ddi_get_lbolt();
	if (timo <= 0)
		return (-1);
	rc = _cv_timedwait_sbt((cvp), &(mp)->lock_object, \
	    tick_sbt * (timo), 0, C_HARDCLOCK);
	if (rc == EWOULDBLOCK)
		return (-1);
	return (1);
}

static inline int
cv_timedwait_sig(kcondvar_t *cvp, kmutex_t *mp, clock_t timo)
{
	int rc;

	timo -= ddi_get_lbolt();
	if (timo <= 0)
		return (-1);
	rc = _cv_timedwait_sig_sbt(cvp, &(mp)->lock_object, \
	    tick_sbt * (timo), 0, C_HARDCLOCK);
	if (rc == EWOULDBLOCK)
		return (-1);
	if (rc == EINTR || rc == ERESTART)
		return (0);

	return (1);
}

#define	cv_timedwait_io cv_timedwait
#define	cv_timedwait_sig_io cv_timedwait_sig

static inline clock_t
cv_timedwait_hires(kcondvar_t *cvp, kmutex_t *mp, hrtime_t tim, hrtime_t res,
    int flag)
{
	hrtime_t hrtime;
	int rc;

	ASSERT(tim >= res);

	hrtime = gethrtime();
	if (flag == 0)
		tim += hrtime;

	if (hrtime >= tim)
		return (-1);

	rc = cv_timedwait_sbt(cvp, mp, zfs_nstosbt(tim),
	    zfs_nstosbt(res), C_ABSOLUTE);

	if (rc == EWOULDBLOCK)
		return (-1);

	KASSERT(rc == 0, ("unexpected rc value %d", rc));
	hrtime = tim - gethrtime();
	if (unlikely(hrtime <= 0)) {
		cv_signal(cvp);
		return (-1);
	}
	return (hrtime);
}

static inline clock_t
cv_timedwait_sig_hires(kcondvar_t *cvp, kmutex_t *mp, hrtime_t tim,
    hrtime_t res, int flag)
{
	sbintime_t sbt;
	hrtime_t hrtime;
	int rc;

	ASSERT(tim >= res);

	hrtime = gethrtime();
	if (flag == 0)
		tim += hrtime;

	if (hrtime >= tim)
		return (-1);

	sbt = zfs_nstosbt(tim);
	rc = cv_timedwait_sig_sbt(cvp, mp, sbt, zfs_nstosbt(res), C_ABSOLUTE);

	switch (rc) {
	case EWOULDBLOCK:
		return (-1);
	case EINTR:
	case ERESTART:
		return (0);
	default:
		KASSERT(rc == 0, ("unexpected rc value %d", rc));
		hrtime = tim - gethrtime();
		if (unlikely(hrtime <= 0)) {
			cv_signal(cvp);
			return (-1);
		}
		return (hrtime);
	}
}

#endif	/* _OPENSOLARIS_SYS_CONDVAR_H_ */
