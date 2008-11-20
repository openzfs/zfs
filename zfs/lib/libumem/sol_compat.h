/*
 * Copyright (c) 2006 OmniTI, Inc. All rights reserved
 * This header file distributed under the terms of the CDDL.
 * Portions Copyright 2004 Sun Microsystems, Inc. All Rights reserved.
 */
#ifndef _EC_UMEM_SOL_COMPAT_H_
#define _EC_UMEM_SOL_COMPAT_H_

#include "config.h"

#include <stdint.h>
#include <pthread.h>

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#ifdef _WIN32
# define THR_RETURN DWORD
# define THR_API WINAPI
# define INLINE __inline
#else
# define THR_RETURN void *
# define THR_API
# define INLINE inline
#endif

#if defined(__MACH__) || defined(_WIN32)
#define NO_WEAK_SYMBOLS
#define _umem_cache_alloc(a,b) umem_cache_alloc(a,b)
#define _umem_cache_free(a,b) umem_cache_free(a,b)
#define _umem_zalloc(a,b) umem_zalloc(a,b)
#define _umem_alloc(a,b) umem_alloc(a,b)
#define _umem_alloc_align(a,b,c) umem_alloc_align(a,b,c)
#define _umem_free(a,b) umem_free(a,b)
#define _umem_free_align(a,b) umem_free_align(a,b)
#endif

#ifdef _WIN32
#define bcopy(s, d, n)  	memcpy(d, s, n)
#define bzero(m, s)			memset(m, 0, s)
#endif

typedef pthread_t thread_t;
typedef pthread_mutex_t mutex_t;
typedef pthread_cond_t cond_t;
typedef u_int64_t hrtime_t;
typedef uint32_t uint_t;
typedef unsigned long ulong_t;
typedef struct timespec timestruc_t;
typedef long long longlong_t;
typedef struct timespec timespec_t;
static INLINE hrtime_t gethrtime(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (((u_int64_t)tv.tv_sec) << 32) | tv.tv_usec;
}
# define thr_self()                pthread_self()
static INLINE thread_t _thr_self(void) {
  return thr_self();
}
#if defined(_MACH_PORT_T)
#define CPUHINT() (pthread_mach_thread_np(pthread_self()))
#endif
# define thr_sigsetmask            pthread_sigmask

#define THR_BOUND     1
#define THR_DETACHED  2
#define THR_DAEMON    4

static INLINE int thr_create(void *stack_base,
  size_t stack_size, THR_RETURN (THR_API *start_func)(void*),
  void *arg, long flags, thread_t *new_thread_ID)
{
  int ret;
  pthread_attr_t attr;

  pthread_attr_init(&attr);

  if (flags & THR_DETACHED) {
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  }
  ret = pthread_create(new_thread_ID, &attr, start_func, arg);
  pthread_attr_destroy(&attr);
  return ret;
}


# define mutex_init(mp, type, arg) pthread_mutex_init(mp, NULL)
# define mutex_lock(mp)            pthread_mutex_lock(mp)
# define mutex_unlock(mp)          pthread_mutex_unlock(mp)
# define mutex_destroy(mp)         pthread_mutex_destroy(mp)
# define mutex_trylock(mp)         pthread_mutex_trylock(mp)
# define DEFAULTMUTEX              PTHREAD_MUTEX_INITIALIZER
# define DEFAULTCV 				   PTHREAD_COND_INITIALIZER
# define MUTEX_HELD(mp)            1 /* not really, but only used in an assert */

# define cond_init(c, type, arg)   pthread_cond_init(c, NULL)
# define cond_wait(c, m)           pthread_cond_wait(c, m)
# define _cond_wait(c, m)          pthread_cond_wait(c, m)
# define cond_signal(c)            pthread_cond_signal(c)
# define cond_broadcast(c)         pthread_cond_broadcast(c)
# define cond_destroy(c)           pthread_cond_destroy(c)
# define cond_timedwait            pthread_cond_timedwait
# define _cond_timedwait           pthread_cond_timedwait

#ifndef RTLD_FIRST
# define RTLD_FIRST 0
#endif

#ifdef ECELERITY
# include "ec_atomic.h"
#else
# ifdef _WIN32
#  define ec_atomic_inc(a)		InterlockedIncrement(a)
#  define ec_atomic_inc64(a)    InterlockedIncrement64(a)
# elif (defined(__i386__) || defined(__x86_64__)) && defined(__GNUC__)
static INLINE uint_t ec_atomic_cas(uint_t *mem, uint_t with, uint_t cmp)
{
  uint_t prev;
  __asm volatile ("lock; cmpxchgl %1, %2"
        : "=a" (prev)
        : "r"    (with), "m" (*(mem)), "0" (cmp)
        : "memory");
  return prev;
}
# elif defined(__sparc__) && defined(__GNUC__)
static INLINE uint_t ec_atomic_cas(uint_t *mem, uint_t with, uint_t cmp)
{
  __asm volatile ("cas [%3],%2,%0"
        : "+r"(with), "=m"(*(mem))
        : "r"(cmp), "r"(mem), "m"(*(mem)) );
  return with;
}
# endif

# ifndef ec_atomic_inc
static INLINE uint_t ec_atomic_inc(uint_t *mem)
{
  register uint_t last;
  do {
    last = *mem;
  } while (ec_atomic_cas(mem, last+1, last) != last);
  return ++last;
}
# endif
# ifndef ec_atomic_inc64
   /* yeah, it's not great.  It's only used to bump failed allocation
    * counts, so it is not critical right now. */
#  define ec_atomic_inc64(a)  (*a)++
# endif

#endif

#define P2PHASE(x, align)    ((x) & ((align) - 1))
#define P2ALIGN(x, align)    ((x) & -(align))
#define P2NPHASE(x, align)    (-(x) & ((align) - 1))
#define P2ROUNDUP(x, align)   (-(-(x) & -(align)))
#define P2END(x, align)     (-(~(x) & -(align)))
#define P2PHASEUP(x, align, phase)  ((phase) - (((phase) - (x)) & -(align)))
#define P2CROSS(x, y, align)    (((x) ^ (y)) > (align) - 1)
#define P2SAMEHIGHBIT(x, y)    (((x) ^ (y)) < ((x) & (y)))
#define IS_P2ALIGNED(v, a) ((((uintptr_t)(v)) & ((uintptr_t)(a) - 1)) == 0)
#define ISP2(x)    (((x) & ((x) - 1)) == 0)

/* beware! umem only uses these atomic adds for incrementing by 1 */
#define atomic_add_64(lvalptr, delta) ec_atomic_inc64(lvalptr)
#define atomic_add_32_nv(a, b)  	  ec_atomic_inc(a) 

#ifndef NANOSEC
#define NANOSEC 1000000000
#endif

#ifdef _WIN32
#define issetugid()		  0
#elif !defined(__FreeBSD__)
#define issetugid()       (geteuid() == 0)
#endif

#define _sysconf(a) sysconf(a)
#define __NORETURN  __attribute__ ((noreturn))

#define EC_UMEM_DUMMY_PCSTACK 1
static INLINE int __nthreads(void)
{
  /* or more; just to force multi-threaded mode */
  return 2;
}

#if (SIZEOF_VOID_P == 8)
# define _LP64 1
#endif

#ifndef MIN
# define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
# define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif


#endif
