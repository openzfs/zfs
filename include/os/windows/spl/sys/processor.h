
#ifndef	_SPL_PROCESSOR_H
#define	_SPL_PROCESSOR_H

#include <sys/types.h>

extern uint32_t getcpuid();

typedef int	processorid_t;

#define	CPUID_FEATURE_PCLMULQDQ		(1<<1)
#define	CPUID_FEATURE_MOVBE		(1<<22)
#define	CPUID_FEATURE_AES		(1<<25)
#define	CPUID_FEATURE_XSAVE		(1<<26)
#define	CPUID_FEATURE_OSXSAVE		(1<<27)
#define	CPUID_FEATURE_AVX1_0		(1<<28)

#define	CPUID_FEATURE_SSE		(1<<25)
#define	CPUID_FEATURE_SSE2		(1<<26)
#define	CPUID_FEATURE_SSE3		(1<<0)
#define	CPUID_FEATURE_SSSE3		(1<<9)
#define	CPUID_FEATURE_SSE4_2		(1<<20)
#define	CPUID_FEATURE_SSE4_1		(1<<19)

#define	CPUID_LEAF7_FEATURE_AVX2    (1<<5)
#define	CPUID_LEAF7_FEATURE_AVX512F    (1<<16)
#define	CPUID_LEAF7_FEATURE_SHA_NI		(1<<29)

#endif /* _SPL_PROCESSOR_H */
