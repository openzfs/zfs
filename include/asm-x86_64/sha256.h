#ifndef	_ASM_SHA256_H
#define	_ASM_SHA256_H

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef _KERNEL
extern void arch_sha256_init(void);
#else
static inline void arch_sha256_init(void) { }
#endif

#ifdef	__cplusplus
}
#endif

#endif /* _ASM_SHA256_H */
