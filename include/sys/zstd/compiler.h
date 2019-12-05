/* BEGIN CSTYLED */
/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef ZSTD_COMPILER_H
#define ZSTD_COMPILER_H

/*-*******************************************************
*  Compiler specifics
*********************************************************/
/* force inlining */
#  define INLINE_KEYWORD inline
#  define FORCE_INLINE_ATTR __always_inline

/* vectorization
 * older GCC (pre gcc-4.3 picked as the cutoff) uses a different syntax */
#if !defined(__clang__) && defined(__GNUC__)
#  if (__GNUC__ == 4 && __GNUC_MINOR__ > 3) || (__GNUC__ >= 5)
#    define DONT_VECTORIZE __attribute__((optimize("no-tree-vectorize")))
#  else
#    define DONT_VECTORIZE _Pragma("GCC optimize(\"no-tree-vectorize\")")
#  endif
#else
#  define DONT_VECTORIZE
#endif

/**
 * FORCE_INLINE_TEMPLATE is used to define C "templates", which take constant
 * parameters. They must be inlined for the compiler to eliminate the constant
 * branches.
 */
#define FORCE_INLINE_TEMPLATE static INLINE_KEYWORD FORCE_INLINE_ATTR
/**
 * HINT_INLINE is used to help the compiler generate better code. It is *not*
 * used for "templates", so it can be tweaked based on the compilers
 * performance.
 *
 * gcc-4.8 and gcc-4.9 have been shown to benefit from leaving off the
 * always_inline attribute.
 *
 * clang up to 5.0.0 (trunk) benefit tremendously from the always_inline
 * attribute.
 */
#if !defined(__clang__) && defined(__GNUC__) && __GNUC__ >= 4 && __GNUC_MINOR__ >= 8 && __GNUC__ < 5
#  define HINT_INLINE static INLINE_KEYWORD
#else
#  define HINT_INLINE static INLINE_KEYWORD FORCE_INLINE_ATTR
#endif

/* UNUSED_ATTR tells the compiler it is okay if the function is unused. */
#if defined(__GNUC__)
#  define UNUSED_ATTR __attribute__((unused))
#else
#  define UNUSED_ATTR
#endif

#ifdef _KERNEL
/* force no inlining */
#  ifdef __GNUC__
#    define FORCE_NOINLINE static noinline
#  else
#    define FORCE_NOINLINE static
#  endif
#else
#  ifdef __GNUC__
#    define FORCE_NOINLINE static __attribute__((noinline))
#  else
#    define FORCE_NOINLINE static
#  endif


#endif

/* target attribute */
#ifndef __has_attribute
  #define __has_attribute(x) 0  /* Compatibility with non-clang compilers. */
#endif
#if defined(__GNUC__)
#  define TARGET_ATTRIBUTE(target) __attribute__((__target__(target)))
#else
#  define TARGET_ATTRIBUTE(target)
#endif

/* Enable runtime BMI2 dispatch based on the CPU.
 * Enabled for clang & gcc >=4.8 on x86 when BMI2 isn't enabled by default.
 */
#ifndef DYNAMIC_BMI2
  #if ((defined(__clang__) && __has_attribute(__target__)) \
      || (defined(__GNUC__) \
          && (__GNUC__ >= 5 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 8)))) \
      && (defined(__x86_64__) || defined(_M_X86)) \
      && !defined(__BMI2__)
  #  define DYNAMIC_BMI2 1
  #else
  #  define DYNAMIC_BMI2 0
  #endif
#endif
/* prefetch 
 * can be disabled, by declaring NO_PREFETCH build macro */
#if defined(NO_PREFETCH)
#  define PREFETCH_L1(ptr)  (void)(ptr)  /* disabled */
#  define PREFETCH_L2(ptr)  (void)(ptr)  /* disabled */
#else
#  if defined(__GNUC__) && ( (__GNUC__ >= 4) || ( (__GNUC__ == 3) && (__GNUC_MINOR__ >= 1) ) )
#    define PREFETCH_L1(ptr)  __builtin_prefetch((ptr), 0 /* rw==read */, 3 /* locality */)
#    define PREFETCH_L2(ptr)  __builtin_prefetch((ptr), 0 /* rw==read */, 2 /* locality */)
#  else
#    define PREFETCH_L1(ptr) (void)(ptr)  /* disabled */
#    define PREFETCH_L2(ptr) (void)(ptr)  /* disabled */
#  endif
#endif  /* NO_PREFETCH */

#if defined(__x86_64__) || defined(__i386__)
#define CACHELINE_SIZE 64
#else
#define CACHELINE_SIZE 32
#endif

#define PREFETCH_AREA(p, s)  {            \
    const char* const _ptr = (const char*)(p);  \
    size_t const _size = (size_t)(s);     \
    size_t _pos;                          \
    for (_pos=0; _pos<_size; _pos+=CACHELINE_SIZE) {  \
        PREFETCH_L2(_ptr + _pos);         \
    }                                     \
}

#endif /* ZSTD_COMPILER_H */
/* END CSTYLED */
