/* aes-without-movbe (bswap) x86_64 GCM implementation
   and shared global utility functions */

#if defined(__x86_64__) && defined(HAVE_AVX) && \
    defined(HAVE_AES) && defined(HAVE_PCLMULQDQ)

# define MOVQ_AND_BYTESWAP(SRC,DEST) \
    movq SRC,DEST \n\
    bswapq DEST
# define ZFS_ENTRY_SYMBOL_RENAME(NAME) NAME##_bswap
# include "aesni-gcm-x86_64.S.inc"

/* Some utility routines */

/*
 * clear all fpu registers
 * void clear_fpu_regs_avx(void);
 */
.globl	clear_fpu_regs_avx
.type	clear_fpu_regs_avx,@function
.align	32
clear_fpu_regs_avx:
	vzeroall
	ret
.size	clear_fpu_regs_avx,.-clear_fpu_regs_avx

/*
 * void gcm_xor_avx(const uint8_t *src, uint8_t *dst);
 *
 * XORs one pair of unaligned 128-bit blocks from `src' and `dst' and
 * stores the result at `dst'. The XOR is performed using FPU registers,
 * so make sure FPU state is saved when running this in the kernel.
 */
.globl  gcm_xor_avx
.type	gcm_xor_avx,@function
.align	32
gcm_xor_avx:
	movdqu  (%rdi), %xmm0
	movdqu  (%rsi), %xmm1
	pxor    %xmm1, %xmm0
	movdqu  %xmm0, (%rsi)
	ret
.size	gcm_xor_avx,.-gcm_xor_avx

/*
 * Toggle a boolean_t value atomically and return the new value.
 * boolean_t atomic_toggle_boolean_nv(volatile boolean_t *);
 */
.globl	atomic_toggle_boolean_nv
.type	atomic_toggle_boolean_nv,@function
.align	32
atomic_toggle_boolean_nv:
	xorl	%eax, %eax
	lock
	xorl	$1, (%rdi)
	jz	1f
	movl	$1, %eax
1:
	ret
.size	atomic_toggle_boolean_nv,.-atomic_toggle_boolean_nv

#endif /* defined(__x86_64__) ... */
