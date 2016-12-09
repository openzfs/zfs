/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

#if defined(_KERNEL) && defined(HAVE_QAT)
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/completion.h>
#include <sys/zfs_context.h>
#include "qat_compress.h"

#define	TIMEOUT_MS		500 /* 0.5 seconds */
#define	INST_NUM		6
#define	GZIP_HEAD_SZ		2
#define	GZIP_FOOT_SZ		4
#define	QAT_MIN_BUF_SIZE	4096

static CpaInstanceHandle dc_inst_handles[INST_NUM];
static CpaDcSessionHandle session_handles[INST_NUM];
static Cpa16U num_inst = 0;
static Cpa16U inst = 0;
int qat_init_done = 0;
int zfs_qat_disable = B_FALSE;

#define	PHYS_CONTIG_ALLOC(pp_mem_addr, size_bytes)	\
	mem_alloc_contig((void *)(pp_mem_addr), (size_bytes))

#define	PHYS_CONTIG_FREE(p_mem_addr)	\
	mem_free_contig((void *)&(p_mem_addr))

static inline struct page *mem_to_page(void *addr)
{
	if (!is_vmalloc_addr(addr))
		return (virt_to_page(addr));

	return (vmalloc_to_page(addr));
}

static void qat_dc_callback(void *p_callback, CpaStatus status)
{
	if (NULL != p_callback)
		complete((struct completion *)p_callback);
}

static inline CpaStatus mem_alloc_contig(void **pp_mem_addr,
						Cpa32U size_bytes)
{
	*pp_mem_addr = kmalloc_node(size_bytes, GFP_KERNEL, 0);
	if (NULL == *pp_mem_addr)
		return (CPA_STATUS_RESOURCE);
	return (CPA_STATUS_SUCCESS);
}

static inline void mem_free_contig(void **pp_mem_addr)
{
	if (NULL != *pp_mem_addr) {
		kfree(*pp_mem_addr);
		*pp_mem_addr = NULL;
	}
}

int
qat_init(void)
{
	CpaStatus status = CPA_STATUS_SUCCESS;
	Cpa32U sess_size = 0;
	Cpa32U ctx_size = 0;
	Cpa32U buff_meta_size = 0;
	CpaDcSessionSetupData sd = {0};
	int i;

	if (zfs_qat_disable == B_TRUE || qat_init_done != 0)
		return (0);

	status = cpaDcGetNumInstances(&num_inst);
	if (status != CPA_STATUS_SUCCESS || num_inst == 0)
		return (-1);
	status = cpaDcGetInstances(num_inst, &dc_inst_handles[0]);
	if (status != CPA_STATUS_SUCCESS)
		return (-1);

	for (i = 0; i < num_inst; i++) {
		cpaDcSetAddressTranslation(dc_inst_handles[i],
		    (void*)virt_to_phys);

		status = cpaDcBufferListGetMetaSize(dc_inst_handles[i], 1,
		    &buff_meta_size);

		status = cpaDcStartInstance(dc_inst_handles[i], 0, NULL);
		if (status != CPA_STATUS_SUCCESS)
			goto fail;

		sd.compLevel = CPA_DC_L1;
		sd.compType = CPA_DC_DEFLATE;
		sd.huffType = CPA_DC_HT_STATIC;
		sd.sessDirection = CPA_DC_DIR_COMBINED;
		sd.sessState = CPA_DC_STATELESS;
		sd.deflateWindowSize = 7;
		sd.checksum = CPA_DC_ADLER32;
		status = cpaDcGetSessionSize(dc_inst_handles[i],
		    &sd, &sess_size, &ctx_size);
		if (status != CPA_STATUS_SUCCESS)
			goto fail;

		PHYS_CONTIG_ALLOC(&session_handles[i], sess_size);
		if (session_handles[i] == NULL)
			goto fail;

		status = cpaDcInitSession(dc_inst_handles[i],
		    session_handles[i],
		    &sd, NULL, qat_dc_callback);
		if (status != CPA_STATUS_SUCCESS)
			goto fail;
	}

	qat_init_done = 1;
	return (0);
fail:

	for (i = 0; i < num_inst; i++) {
		cpaDcStopInstance(dc_inst_handles[i]);
		PHYS_CONTIG_FREE(session_handles[i]);
	}

	return (-1);
}

void
qat_fini(void)
{
	int i = 0;
	if (zfs_qat_disable == B_TRUE || qat_init_done == 0)
		return;

	for (i = 0; i < num_inst; i++) {
		cpaDcStopInstance(dc_inst_handles[i]);
		PHYS_CONTIG_FREE(session_handles[i]);
	}
	num_inst = 0;
	qat_init_done = 0;
}

int
use_qat(size_t s_len)
{
	if (zfs_qat_disable == B_TRUE ||
	    qat_init_done == 0 ||
	    s_len <= QAT_MIN_BUF_SIZE) {
		return (0);
	}
	return (1);
}

int
qat_compress(int dir, char *src, int src_len,
	    char *dst, int dst_len, size_t *c_len)
{
	CpaInstanceHandle dc_inst_handle;
	CpaDcSessionHandle session_handle;
	CpaBufferList *buf_list_src = NULL;
	CpaBufferList *buf_list_dst = NULL;
	CpaFlatBuffer *flat_buf_src = NULL;
	CpaFlatBuffer *flat_buf_dst = NULL;
	Cpa8U *buffer_meta_src = NULL;
	Cpa8U *buffer_meta_dst = NULL;
	Cpa32U buffer_meta_size = 0;
	CpaDcRqResults dc_results;
	CpaStatus status;
	Cpa32U hdr_sz = 0;
	Cpa32U compressed_sz;
	Cpa32U num_src_buf = (src_len >> PAGE_SHIFT) + 1;
	Cpa32U num_dst_buf = (dst_len >> PAGE_SHIFT) + 1;
	Cpa32U bytes_left;
	char *data;
	struct page *in_page, *out_page;
	struct page **in_pages = NULL;
	struct page **out_pages = NULL;
	struct completion complete;
	size_t ret = -1;
	int page_num = 0;
	int i;

	Cpa32U src_buffer_list_mem_size = sizeof (CpaBufferList) +
	    (num_src_buf * sizeof (CpaFlatBuffer));
	Cpa32U dst_buffer_list_mem_size = sizeof (CpaBufferList) +
	    (num_dst_buf * sizeof (CpaFlatBuffer));

	if (!is_vmalloc_addr(src) || !is_vmalloc_addr(src + src_len - 1) ||
	    !is_vmalloc_addr(dst) || !is_vmalloc_addr(dst + dst_len - 1))
		return (-1);

	if (PHYS_CONTIG_ALLOC(&in_pages,
	    num_src_buf * sizeof (struct page *)) != CPA_STATUS_SUCCESS)
		goto fail;

	if (PHYS_CONTIG_ALLOC(&out_pages,
	    num_dst_buf * sizeof (struct page *)) != CPA_STATUS_SUCCESS)
		goto fail;

	inst = (inst + 1) % num_inst;
	dc_inst_handle = dc_inst_handles[inst];
	session_handle = session_handles[inst];

	cpaDcBufferListGetMetaSize(dc_inst_handle, num_src_buf,
	    &buffer_meta_size);
	if (PHYS_CONTIG_ALLOC(&buffer_meta_src, buffer_meta_size) !=
	    CPA_STATUS_SUCCESS)
		goto fail;

	cpaDcBufferListGetMetaSize(dc_inst_handle, num_dst_buf,
	    &buffer_meta_size);
	if (PHYS_CONTIG_ALLOC(&buffer_meta_dst, buffer_meta_size) !=
	    CPA_STATUS_SUCCESS)
		goto fail;

	/* build source buffer list */
	if (PHYS_CONTIG_ALLOC(&buf_list_src, src_buffer_list_mem_size) !=
	    CPA_STATUS_SUCCESS)
		goto fail;

	flat_buf_src = (CpaFlatBuffer *)(buf_list_src + 1);

	buf_list_src->pBuffers = flat_buf_src; /* always point to first one */

	/* build destination buffer list */
	if (PHYS_CONTIG_ALLOC(&buf_list_dst, dst_buffer_list_mem_size) !=
	    CPA_STATUS_SUCCESS)
		goto fail;

	flat_buf_dst = (CpaFlatBuffer *)(buf_list_dst + 1);

	buf_list_dst->pBuffers = flat_buf_dst; /* always point to first one */

	buf_list_src->numBuffers = 0;
	buf_list_src->pPrivateMetaData = buffer_meta_src;
	bytes_left = src_len;
	data = src;
	page_num = 0;
	while (bytes_left > 0) {
		in_page = mem_to_page(data);
		in_pages[page_num] = in_page;
		flat_buf_src->pData = kmap(in_page);
		flat_buf_src->dataLenInBytes =
		    min((long)bytes_left, (long)PAGE_SIZE);

		bytes_left -= flat_buf_src->dataLenInBytes;
		data += flat_buf_src->dataLenInBytes;
		flat_buf_src++;
		buf_list_src->numBuffers++;
		page_num++;
	}

	buf_list_dst->numBuffers = 0;
	buf_list_dst->pPrivateMetaData = buffer_meta_dst;
	bytes_left = dst_len;
	data = dst;
	page_num = 0;
	while (bytes_left > 0) {
		out_page = mem_to_page(data);
		flat_buf_dst->pData = kmap(out_page);
		out_pages[page_num] = out_page;
		flat_buf_dst->dataLenInBytes =
		    min((long)bytes_left, (long)PAGE_SIZE);

		bytes_left -= flat_buf_dst->dataLenInBytes;
		data += flat_buf_dst->dataLenInBytes;
		flat_buf_dst++;
		buf_list_dst->numBuffers++;
		page_num++;
	}

	init_completion(&complete);

	if (dir == 0) /* compress */ {
		cpaDcGenerateHeader(session_handle,
		    buf_list_dst->pBuffers, &hdr_sz);
		buf_list_dst->pBuffers->pData += hdr_sz;
		buf_list_dst->pBuffers->dataLenInBytes -= hdr_sz;
		status = cpaDcCompressData(dc_inst_handle, session_handle,
		    buf_list_src, buf_list_dst,
		    &dc_results, CPA_DC_FLUSH_FINAL,
		    &complete);
		if (CPA_STATUS_SUCCESS != status) {
			printk(KERN_INFO
			    "cpaDcCompressData failed. (status = %d)\n",
			    status);
			goto fail;
		}
		/* we now wait until the completion of the operation. */
		if (!wait_for_completion_interruptible_timeout(&complete,
		    TIMEOUT_MS)) {
			printk(KERN_ERR
			    "timeout or interruption in cpaDcCompressData\n");
			status = CPA_STATUS_FAIL;
			goto fail;
		}

		if (dc_results.status != CPA_STATUS_SUCCESS) {
			printk(KERN_INFO "cpaDcCompressData failed %d.\n",
			    dc_results.status);
			goto fail;
		}

		compressed_sz = dc_results.produced;
		if (compressed_sz + hdr_sz + GZIP_FOOT_SZ > dst_len) {
			printk(KERN_INFO "overflow\n");
			goto fail;
		}

		flat_buf_dst = (CpaFlatBuffer *)(buf_list_dst + 1);
		/* move to the last page */
		flat_buf_dst += (compressed_sz + hdr_sz) >> PAGE_SHIFT;

		/* no space for gzip foot in the last page */
		if (((compressed_sz + hdr_sz) % PAGE_SIZE)
		    + GZIP_FOOT_SZ > PAGE_SIZE)
			goto fail;

		flat_buf_dst->pData += (compressed_sz + hdr_sz) % PAGE_SIZE;
		flat_buf_dst->dataLenInBytes = GZIP_FOOT_SZ;

		dc_results.produced = 0;
		/* write RFC1952 gzip footer to destination buffer */
		status = cpaDcGenerateFooter(session_handle,
		    flat_buf_dst, &dc_results);
		*c_len = compressed_sz + dc_results.produced + hdr_sz;

		if (*c_len < PAGE_SIZE)
			*c_len = 8 * PAGE_SIZE;
	} else /* de-compress */ {
		buf_list_src->pBuffers->pData += GZIP_HEAD_SZ;
		buf_list_src->pBuffers->dataLenInBytes -= GZIP_HEAD_SZ;
		status = cpaDcDecompressData(dc_inst_handle,
		    session_handle,
		    buf_list_src,
		    buf_list_dst,
		    &dc_results,
		    CPA_DC_FLUSH_FINAL,
		    &complete);

		if (CPA_STATUS_SUCCESS != status) {
			printk(KERN_INFO
			    "cpaDcDecompressData failed. (status = %d)\n",
			    dc_results.status);
			goto fail;
		}
		/* we now wait until the completion of the operation. */
		if (!wait_for_completion_interruptible_timeout(&complete,
		    TIMEOUT_MS)) {
			printk(KERN_ERR
			    "timeout or interruption in cpaDcCompressData\n");
			goto fail;
		}

		if (dc_results.status != CPA_STATUS_SUCCESS)
			goto fail;

		*c_len = dc_results.produced;
	}

	ret = 0;
fail:
	if (in_pages) {
		for (i = 0; i < buf_list_src->numBuffers; i++)
			kunmap(in_pages[i]);
		PHYS_CONTIG_FREE(in_pages);
	}
	if (out_pages) {
		for (i = 0; i < buf_list_dst->numBuffers; i++)
			kunmap(out_pages[i]);
		PHYS_CONTIG_FREE(out_pages);
	}
	PHYS_CONTIG_FREE(buffer_meta_src);
	PHYS_CONTIG_FREE(buffer_meta_dst);
	PHYS_CONTIG_FREE(buf_list_src);
	PHYS_CONTIG_FREE(buf_list_dst);
	return (ret);
}
module_param(zfs_qat_disable, int, 0644);
MODULE_PARM_DESC(zfs_qat_disable, "Disable QAT compression");
#endif
