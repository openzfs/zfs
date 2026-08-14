// SPDX-License-Identifier: CDDL-1.0
#ifndef _OS_NETBSD_LINUX_COMPILER_H_
#define	_OS_NETBSD_LINUX_COMPILER_H_

#ifndef __aligned
#define	__aligned(x)	__attribute__((__aligned__(x)))
#endif
#ifndef noinline
#define	noinline	__attribute__((noinline))
#endif
#ifndef __unused
#define	__unused	__attribute__((unused))
#endif
#ifndef ____cacheline_aligned
#define	____cacheline_aligned	__attribute__((__aligned__(64)))
#endif
#ifndef likely
#define	likely(x)	__builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
#define	unlikely(x)	__builtin_expect(!!(x), 0)
#endif
#ifndef __always_inline
#define	__always_inline	inline __attribute__((always_inline))
#endif

#endif /* _OS_NETBSD_LINUX_COMPILER_H_ */
