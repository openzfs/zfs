// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */

#if defined(_KERNEL) && defined(HAVE_QAT)
#include <sys/zfs_context.h>
#include <sys/qat.h>

qat_stats_t qat_stats = {
	{ "comp_requests",			KSTAT_DATA_UINT64 },
	{ "comp_total_in_bytes",		KSTAT_DATA_UINT64 },
	{ "comp_total_out_bytes",		KSTAT_DATA_UINT64 },
	{ "decomp_requests",			KSTAT_DATA_UINT64 },
	{ "decomp_total_in_bytes",		KSTAT_DATA_UINT64 },
	{ "decomp_total_out_bytes",		KSTAT_DATA_UINT64 },
	{ "dc_fails",				KSTAT_DATA_UINT64 },
	{ "encrypt_requests",			KSTAT_DATA_UINT64 },
	{ "encrypt_total_in_bytes",		KSTAT_DATA_UINT64 },
	{ "encrypt_total_out_bytes",		KSTAT_DATA_UINT64 },
	{ "decrypt_requests",			KSTAT_DATA_UINT64 },
	{ "decrypt_total_in_bytes",		KSTAT_DATA_UINT64 },
	{ "decrypt_total_out_bytes",		KSTAT_DATA_UINT64 },
	{ "crypt_fails",			KSTAT_DATA_UINT64 },
	{ "cksum_requests",			KSTAT_DATA_UINT64 },
	{ "cksum_total_in_bytes",		KSTAT_DATA_UINT64 },
	{ "cksum_fails",			KSTAT_DATA_UINT64 },
};

static kstat_t *qat_ksp = NULL;

CpaStatus
qat_mem_alloc_contig(void **pp_mem_addr, Cpa32U size_bytes)
{
	*pp_mem_addr = kmalloc(size_bytes, GFP_KERNEL);
	if (*pp_mem_addr == NULL)
		return (CPA_STATUS_RESOURCE);
	return (CPA_STATUS_SUCCESS);
}

void
qat_mem_free_contig(void **pp_mem_addr)
{
	if (*pp_mem_addr != NULL) {
		kfree(*pp_mem_addr);
		*pp_mem_addr = NULL;
	}
}

int
qat_init(void)
{
	qat_ksp = kstat_create("zfs", 0, "qat", "misc",
	    KSTAT_TYPE_NAMED, sizeof (qat_stats) / sizeof (kstat_named_t),
	    KSTAT_FLAG_VIRTUAL);
	if (qat_ksp != NULL) {
		qat_ksp->ks_data = &qat_stats;
		kstat_install(qat_ksp);
	}

	/*
	 * Just set the disable flag when qat init failed, qat can be
	 * turned on again in post-process after zfs module is loaded, e.g.:
	 * echo 0 > /sys/module/zfs/parameters/zfs_qat_compress_disable
	 */
	if (qat_dc_init() != 0)
		zfs_qat_compress_disable = 1;

	if (qat_cy_init() != 0) {
		zfs_qat_checksum_disable = 1;
		zfs_qat_encrypt_disable = 1;
	}

	return (0);
}

void
qat_fini(void)
{
	if (qat_ksp != NULL) {
		kstat_delete(qat_ksp);
		qat_ksp = NULL;
	}

	qat_cy_fini();
	qat_dc_fini();
}

#endif
