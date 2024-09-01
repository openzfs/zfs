#include <sys/zfs_context.h>
#include <sys/kstat.h>
#include <sys/simd.h>


#ifdef _KERNEL
#ifdef __linux__
#include <linux/simd.h>
#endif /* __linux__ */
kstat_t *simd_stat_kstat;
#endif /* _KERNEL */

#ifdef _KERNEL
/* shut up making this more lines helps nobody */
/* CSTYLED */
#define SIMD_STAT_PRINT(s, feat, val)	kmem_scnprintf(s + off, 4095 - off, "%-16s\t%1d\n", feat, (val))

static int
simd_stat_kstat_data(char *buf, size_t size, void *data)
{
	(void) data;

	static char simd_stat_kstat_payload[4096] = {0};
	static int off = 0;
#ifdef __linux__
	if (off == 0) {
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "kfpu_allowed", kfpu_allowed());
#ifdef __x86__
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "kfpu", HAVE_KERNEL_FPU);
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "kfpu_internal", HAVE_KERNEL_FPU_INTERNAL);
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "__kernel_fpu", HAVE_UNDERSCORE_KERNEL_FPU);
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "sse", zfs_sse_available());
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "sse2", zfs_sse2_available());
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "sse3", zfs_sse3_available());
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "ssse3", zfs_ssse3_available());
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "sse41", zfs_sse4_1_available());
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "sse42", zfs_sse4_2_available());
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "avx", zfs_avx_available());
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "avx2", zfs_avx2_available());
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "avx512f", zfs_avx512f_available());
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "avx512cd", zfs_avx512cd_available());
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "avx512er", zfs_avx512er_available());
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "avx512pf", zfs_avx512pf_available());
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "avx512bw", zfs_avx512bw_available());
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "avx512dq", zfs_avx512dq_available());
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "avx512vl", zfs_avx512vl_available());
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "avx512ifma", zfs_avx512ifma_available());
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "avx512vbmi", zfs_avx512vbmi_available());
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "ymm", __ymm_enabled());
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "zmm", __zmm_enabled());
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "bmi1", zfs_bmi1_available());
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "bmi2", zfs_bmi2_available());
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "aes", zfs_aes_available());
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "pclmulqdq", zfs_pclmulqdq_available());
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "movbe", zfs_movbe_available());

		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "osxsave", boot_cpu_has(X86_FEATURE_OSXSAVE));
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "xsaves", static_cpu_has(X86_FEATURE_XSAVES));
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "xsaveopt", static_cpu_has(X86_FEATURE_XSAVEOPT));
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "xsave", static_cpu_has(X86_FEATURE_XSAVE));
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "fxsr", static_cpu_has(X86_FEATURE_FXSR));
#endif /* __x86__ */
#if defined(__arm__) || defined(__aarch64__)
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "kernel_neon", HAVE_KERNEL_NEON);
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "kernel_mode_neon", CONFIG_KERNEL_MODE_NEON);
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "neon", zfs_neon_available());
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "sha256", zfs_sha256_available());
#if defined(__aarch64__)
		/*
		 * This technically can exist on 32b ARM but we don't
		 * define hooks to check for it and I didn't want to
		 * learn enough ARM ASM to add one.
		 */
		off += SIMD_STAT_PRINT(simd_stat_kstat_payload,
		    "sha512", zfs_sha512_available());
#endif /* __aarch64__ */
#endif /* __arm__ */
	}

	// we need one more for the trailing newline, or this prints grossly.
	off += 1;
	kmem_scnprintf(buf, MIN(off, size), "%s", simd_stat_kstat_payload);
#endif /* __linux__ */
	return (0);
}
#endif /* _KERNEL */

void
simd_stat_init(void)
{
	static boolean_t simd_stat_initialized = B_FALSE;

	if (!simd_stat_initialized) {
#if defined(_KERNEL)
		/* Install kstats for all implementations */
		simd_stat_kstat = kstat_create("zfs", 0, "simd", "misc",
		    KSTAT_TYPE_RAW, 0, KSTAT_FLAG_VIRTUAL);


		if (simd_stat_kstat != NULL) {
			simd_stat_kstat->ks_data = (void*)(uintptr_t)1;
			simd_stat_kstat->ks_ndata = 1;
			simd_stat_kstat->ks_flags |= KSTAT_FLAG_NO_HEADERS;
			kstat_set_raw_ops(simd_stat_kstat,
			    NULL,
			    simd_stat_kstat_data,
			    NULL);
			kstat_install(simd_stat_kstat);
		}
#endif /* _KERNEL */
	}
	/* Finish initialization */
	simd_stat_initialized = B_TRUE;
}

void
simd_stat_fini(void)
{
#if defined(_KERNEL)
	if (simd_stat_kstat != NULL) {
		kstat_delete(simd_stat_kstat);
		simd_stat_kstat = NULL;
	}
#endif
}

#ifdef _KERNEL
EXPORT_SYMBOL(simd_stat_init);
EXPORT_SYMBOL(simd_stat_fini);
#endif
