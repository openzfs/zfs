
#include <sys/zfs_pmem.h>

#include <sys/simd.h>

static void
pmem_avx512_drain_impl(void)
{
	__asm("sfence");
}

static void
pmem_avx512_memcpy256_nt_nodrain(void *dst, const void *buf, size_t size)
{
	ASSERT0(((uintptr_t)dst) % 64); /* 64 byte alignment required by vmovntdq */
	ASSERT0(size % 4*64); /* code below only works at that granularity */

	kfpu_begin();

	/*
	 * by using userspace library, gdb breakpoint on pmem_memcpy_nodrain
	 * and single stepping instructions until we knew what libpmem does:
		<memmove_movnt_avx512f_clwb+118> vmovdqu64 0x40(%rsi),%zmm30                                                                                                      │
		<memmove_movnt_avx512f_clwb+125> vmovdqu64 0x80(%rsi),%zmm29
		...
		<memmove_movnt_avx512f_clwb+341> vmovntdq %zmm30,0x40(%rax)                                                                                                       │
		<memmove_movnt_avx512f_clwb+348> vmovntdq %zmm29,0x80(%rax)
	*/
	const uint8_t *i;
	uint8_t *o;
	size_t resid = size;
	for (i = buf, o = dst; resid > 0; resid -= 4*64, i += 4*64, o += 4*64) {
		/* load dram data to avx register */
		__asm("vmovdqu64 %0, %%zmm0" :: "m" (*(i + 0*64)));
		__asm("vmovdqu64 %0, %%zmm1" :: "m" (*(i + 1*64)));
		__asm("vmovdqu64 %0, %%zmm2" :: "m" (*(i + 2*64)));
		__asm("vmovdqu64 %0, %%zmm3" :: "m" (*(i + 3*64)));

		/* write register contents to pmem with non-temporal hint */
		__asm("vmovntdq %%zmm0, %0" : "=m"  (*(o + 0*64)));
		__asm("vmovntdq %%zmm1, %0" : "=m"  (*(o + 1*64)));
		__asm("vmovntdq %%zmm2, %0" : "=m"  (*(o + 2*64)));
		__asm("vmovntdq %%zmm3, %0" : "=m"  (*(o + 3*64)));
	}

	kfpu_end();
}


static void
pmem_avx512_memzero256_nt_nodrain(void *dst, size_t size)
{
	ASSERT0(((uintptr_t)dst) % 64); /* 64 byte alignment required by vmovntdq */
	ASSERT0(size % 4*64); /* code below only works at that granularity */

	kfpu_begin();

	/*
	 * Zero out zmm{0,1,2,3}
	 * https://stackoverflow.com/a/44585445/305410
	 *
	 * TODO the SO post says something about a mask - have we set this?
	 * => also review other places where we do inline assembly.
	 */
	__asm("vpxord      %zmm3, %zmm3, %zmm3");
	__asm("vmovdqa64   %zmm3, %zmm2");
	__asm("vmovdqa64   %zmm3, %zmm1");
	__asm("vmovdqa64   %zmm3, %zmm0");

	uint8_t *o;
	size_t resid = size;
	for (o = dst; resid > 0; resid -= 4*64, o += 4*64) {
		/* write register contents to pmem with non-temporal hint */
		__asm("vmovntdq %%zmm0, %0" : "=m"  (*(o + 0*64)));
		__asm("vmovntdq %%zmm1, %0" : "=m"  (*(o + 1*64)));
		__asm("vmovntdq %%zmm2, %0" : "=m"  (*(o + 2*64)));
		__asm("vmovntdq %%zmm3, %0" : "=m"  (*(o + 3*64)));
	}

	kfpu_end();
}


static boolean_t
pmem_avx512_check_supported(void)
{
	return (kfpu_allowed() && zfs_avx512f_available());
}


zfs_pmem_ops_t pmem_ops_avx512 = {
	.zpmem_op_name = "avx512",
	.zpmem_op_check_supported = pmem_avx512_check_supported,
	.zpmem_op_memcpy256_nt_nodrain = pmem_avx512_memcpy256_nt_nodrain,
	.zpmem_op_memzero256_nt_nodrain = pmem_avx512_memzero256_nt_nodrain,
	.zpmem_op_drain = pmem_avx512_drain_impl
};
