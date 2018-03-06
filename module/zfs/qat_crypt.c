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
#include <sys/zio_crypt.h>
#include "lac/cpa_cy_im.h"
#include "qat.h"

/*
 * Max instances in QAT device, each instance is a channel to submit
 * jobs to QAT hardware, this is only for pre-allocating instance,
 * and session arrays, the actual number of instances are defined in
 * the QAT driver's configure file.
 */
#define	QAT_CRYPT_MAX_INSTANCES		48

#define	MAX_PAGE_NUM			1024

static boolean_t qat_crypt_init_done = B_FALSE;
static Cpa16U inst_num = 0;
static Cpa16U num_inst = 0;
static CpaInstanceHandle cy_inst_handles[QAT_CRYPT_MAX_INSTANCES];

typedef struct cy_callback {
	CpaBoolean verify_result;
	struct completion complete;
} cy_callback_t;

static void
symcallback(void *p_callback, CpaStatus status, const CpaCySymOp operation,
    void *op_data, CpaBufferList *buf_list_dst, CpaBoolean verify)
{
	cy_callback_t *cb = p_callback;

	if (cb != NULL) {
		/* indicate that the function has been called */
		cb->verify_result = verify;
		complete(&cb->complete);
	}
}

boolean_t
qat_crypt_use_accel(size_t s_len)
{
	return (!zfs_qat_disable &&
	    qat_crypt_init_done &&
	    s_len >= QAT_MIN_BUF_SIZE &&
	    s_len <= QAT_MAX_BUF_SIZE);
}

void
qat_crypt_clean(void)
{
	for (Cpa32U i = 0; i < num_inst; i++)
		cpaCyStopInstance(cy_inst_handles[i]);

	num_inst = 0;
	qat_crypt_init_done = B_FALSE;
}

int
qat_crypt_init(void)
{
	Cpa32U i;
	CpaStatus status = CPA_STATUS_FAIL;

	status = cpaCyGetNumInstances(&num_inst);
	if (status != CPA_STATUS_SUCCESS)
		return (-1);

	/* if the user has configured no QAT encryption units just return */
	if (num_inst == 0)
		return (0);

	if (num_inst > QAT_CRYPT_MAX_INSTANCES)
		num_inst = QAT_CRYPT_MAX_INSTANCES;

	status = cpaCyGetInstances(num_inst, &cy_inst_handles[0]);
	if (status != CPA_STATUS_SUCCESS)
		return (-1);

	for (i = 0; i < num_inst; i++) {
		status = cpaCySetAddressTranslation(cy_inst_handles[i],
		    (void *)virt_to_phys);
		if (status != CPA_STATUS_SUCCESS)
			goto error;

		status = cpaCyStartInstance(cy_inst_handles[i]);
		if (status != CPA_STATUS_SUCCESS)
			goto error;
	}

	qat_crypt_init_done = B_TRUE;
	return (0);

error:
	qat_crypt_clean();
	return (-1);
}

void
qat_crypt_fini(void)
{
	if (!qat_crypt_init_done)
		return;

	qat_crypt_clean();
}

static CpaStatus
init_cy_session_ctx(qat_encrypt_dir_t dir, CpaInstanceHandle inst_handle,
    CpaCySymSessionCtx **cy_session_ctx, crypto_key_t *key,
    Cpa64U crypt, Cpa32U aad_len)
{
	CpaStatus status = CPA_STATUS_SUCCESS;
	Cpa32U ctx_size;
	Cpa32U ciper_algorithm;
	Cpa32U hash_algorithm;
	CpaCySymSessionSetupData sd = { 0 };

	if (zio_crypt_table[crypt].ci_crypt_type == ZC_TYPE_CCM) {
		return (CPA_STATUS_FAIL);
	} else {
		ciper_algorithm = CPA_CY_SYM_CIPHER_AES_GCM;
		hash_algorithm = CPA_CY_SYM_HASH_AES_GCM;
	}

	sd.cipherSetupData.cipherAlgorithm = ciper_algorithm;
	sd.cipherSetupData.pCipherKey = key->ck_data;
	sd.cipherSetupData.cipherKeyLenInBytes = key->ck_length / 8;
	sd.hashSetupData.hashAlgorithm = hash_algorithm;
	sd.hashSetupData.hashMode = CPA_CY_SYM_HASH_MODE_AUTH;
	sd.hashSetupData.digestResultLenInBytes = ZIO_DATA_MAC_LEN;
	sd.hashSetupData.authModeSetupData.aadLenInBytes = aad_len;
	sd.sessionPriority = CPA_CY_PRIORITY_NORMAL;
	sd.symOperation = CPA_CY_SYM_OP_ALGORITHM_CHAINING;
	sd.digestIsAppended = CPA_FALSE;
	sd.verifyDigest = CPA_FALSE;

	if (dir == QAT_ENCRYPT) {
		sd.cipherSetupData.cipherDirection =
		    CPA_CY_SYM_CIPHER_DIRECTION_ENCRYPT;
		sd.algChainOrder =
		    CPA_CY_SYM_ALG_CHAIN_ORDER_HASH_THEN_CIPHER;
	} else {
		ASSERT3U(dir, ==, QAT_DECRYPT);
		sd.cipherSetupData.cipherDirection =
		    CPA_CY_SYM_CIPHER_DIRECTION_DECRYPT;
		sd.algChainOrder =
		    CPA_CY_SYM_ALG_CHAIN_ORDER_CIPHER_THEN_HASH;
	}

	status = cpaCySymSessionCtxGetSize(inst_handle, &sd, &ctx_size);
	if (status != CPA_STATUS_SUCCESS)
		return (status);

	status = QAT_PHYS_CONTIG_ALLOC(cy_session_ctx, ctx_size);
	if (status != CPA_STATUS_SUCCESS)
		return (status);

	status = cpaCySymInitSession(inst_handle, symcallback, &sd,
	    *cy_session_ctx);
	if (status != CPA_STATUS_SUCCESS) {
		QAT_PHYS_CONTIG_FREE(*cy_session_ctx);
		return (status);
	}

	return (CPA_STATUS_SUCCESS);
}

static CpaStatus
init_cy_buffer_lists(CpaInstanceHandle inst_handle, uint32_t nr_bufs,
    CpaBufferList *src, CpaBufferList *dst)
{
	CpaStatus status = CPA_STATUS_SUCCESS;
	Cpa32U meta_size = 0;

	status = cpaCyBufferListGetMetaSize(inst_handle, nr_bufs, &meta_size);
	if (status != CPA_STATUS_SUCCESS)
		return (status);

	src->numBuffers = nr_bufs;
	status = QAT_PHYS_CONTIG_ALLOC(&src->pPrivateMetaData, meta_size);
	if (status != CPA_STATUS_SUCCESS)
		goto error;

	if (src != dst) {
		dst->numBuffers = nr_bufs;
		status = QAT_PHYS_CONTIG_ALLOC(&dst->pPrivateMetaData,
		    meta_size);
		if (status != CPA_STATUS_SUCCESS)
			goto error;
	}

	return (CPA_STATUS_SUCCESS);

error:
	QAT_PHYS_CONTIG_FREE(src->pPrivateMetaData);
	if (src != dst)
		QAT_PHYS_CONTIG_FREE(dst->pPrivateMetaData);

	return (status);
}

int
qat_crypt(qat_encrypt_dir_t dir, uint8_t *src_buf, uint8_t *dst_buf,
    uint8_t *aad_buf, uint32_t aad_len, uint8_t *iv_buf, uint8_t *digest_buf,
    crypto_key_t *key, uint64_t crypt, uint32_t enc_len)
{
	CpaStatus status = CPA_STATUS_SUCCESS;
	Cpa16U i;
	CpaInstanceHandle cy_inst_handle;
	Cpa16U nr_bufs;
	Cpa32U bytes_left = 0;
	Cpa8S *in = NULL;
	Cpa8S *out = NULL;
	CpaCySymSessionCtx *cy_session_ctx = NULL;
	cy_callback_t cb;
	CpaCySymOpData op_data = { 0 };
	CpaBufferList src_buffer_list = { 0 };
	CpaBufferList dst_buffer_list = { 0 };
	CpaFlatBuffer *flat_src_buf_array = NULL;
	CpaFlatBuffer *flat_src_buf = NULL;
	CpaFlatBuffer *flat_dst_buf_array = NULL;
	CpaFlatBuffer *flat_dst_buf = NULL;
	struct page *in_pages[MAX_PAGE_NUM];
	struct page *out_pages[MAX_PAGE_NUM];
	Cpa32S page_num = 0;

	if (dir == QAT_ENCRYPT) {
		QAT_STAT_BUMP(encrypt_requests);
		QAT_STAT_INCR(encrypt_total_in_bytes, enc_len);
	} else {
		QAT_STAT_BUMP(decrypt_requests);
		QAT_STAT_INCR(decrypt_total_in_bytes, enc_len);
	}

	i = atomic_inc_32_nv(&inst_num) % num_inst;
	cy_inst_handle = cy_inst_handles[i];

	status = init_cy_session_ctx(dir, cy_inst_handle, &cy_session_ctx, key,
	    crypt, aad_len);
	if (status != CPA_STATUS_SUCCESS)
		return (status);

	nr_bufs = enc_len / PAGE_CACHE_SIZE +
	    (enc_len % PAGE_CACHE_SIZE == 0 ? 0 : 1);
	status = init_cy_buffer_lists(cy_inst_handle, nr_bufs, &src_buffer_list,
	    &dst_buffer_list);
	if (status != CPA_STATUS_SUCCESS)
		goto fail;

	status = QAT_PHYS_CONTIG_ALLOC(&flat_src_buf_array,
	    nr_bufs * sizeof (CpaFlatBuffer));
	if (status != CPA_STATUS_SUCCESS)
		goto fail;
	status = QAT_PHYS_CONTIG_ALLOC(&flat_dst_buf_array,
	    nr_bufs * sizeof (CpaFlatBuffer));
	if (status != CPA_STATUS_SUCCESS)
		goto fail;

	bytes_left = enc_len;
	in = src_buf;
	out = dst_buf;
	flat_src_buf = flat_src_buf_array;
	flat_dst_buf = flat_dst_buf_array;
	while (bytes_left > 0) {
		in_pages[page_num] = qat_mem_to_page(in);
		out_pages[page_num] = qat_mem_to_page(out);
		flat_src_buf->pData = kmap(in_pages[page_num]);
		flat_dst_buf->pData = kmap(out_pages[page_num]);
		flat_src_buf->dataLenInBytes = min((long)PAGE_CACHE_SIZE,
		    (long)bytes_left);
		flat_dst_buf->dataLenInBytes = min((long)PAGE_CACHE_SIZE,
		    (long)bytes_left);
		in += flat_src_buf->dataLenInBytes;
		out += flat_dst_buf->dataLenInBytes;
		bytes_left -= flat_src_buf->dataLenInBytes;
		flat_src_buf++;
		flat_dst_buf++;
		page_num++;
	}
	src_buffer_list.pBuffers = flat_src_buf_array;
	dst_buffer_list.pBuffers = flat_dst_buf_array;

	op_data.sessionCtx = cy_session_ctx;
	op_data.packetType = CPA_CY_SYM_PACKET_TYPE_FULL;
	op_data.pIv = NULL; /* set this later as the J0 block */
	op_data.ivLenInBytes = 0;
	op_data.cryptoStartSrcOffsetInBytes = 0;
	op_data.messageLenToCipherInBytes = 0;
	op_data.hashStartSrcOffsetInBytes = 0;
	op_data.messageLenToHashInBytes = 0;
	op_data.pDigestResult = 0;
	op_data.messageLenToCipherInBytes = enc_len;
	op_data.ivLenInBytes = ZIO_DATA_IV_LEN;
	op_data.pDigestResult = digest_buf;
	op_data.pAdditionalAuthData = aad_buf;
	op_data.pIv = iv_buf;

	cb.verify_result = CPA_FALSE;
	init_completion(&cb.complete);
	status = cpaCySymPerformOp(cy_inst_handle, &cb, &op_data,
	    &src_buffer_list, &dst_buffer_list, NULL);
	if (status != CPA_STATUS_SUCCESS)
		goto fail;

	if (!wait_for_completion_interruptible_timeout(&cb.complete,
	    QAT_TIMEOUT_MS)) {
		status = CPA_STATUS_FAIL;
		goto fail;
	}

	if (cb.verify_result == CPA_FALSE) {
		status = CPA_STATUS_FAIL;
		goto fail;
	}

	if (dir == QAT_ENCRYPT)
		QAT_STAT_INCR(encrypt_total_out_bytes, enc_len);
	else
		QAT_STAT_INCR(decrypt_total_out_bytes, enc_len);

fail:
	/* don't count CCM as a failure since it's not supported */
	if (status != CPA_STATUS_SUCCESS &&
	    zio_crypt_table[crypt].ci_crypt_type != ZC_TYPE_CCM)
		QAT_STAT_BUMP(crypt_fails);

	for (i = 0; i < page_num; i ++) {
		kunmap(in_pages[i]);
		kunmap(out_pages[i]);
	}

	cpaCySymRemoveSession(cy_inst_handle, cy_session_ctx);
	QAT_PHYS_CONTIG_FREE(src_buffer_list.pPrivateMetaData);
	QAT_PHYS_CONTIG_FREE(dst_buffer_list.pPrivateMetaData);
	QAT_PHYS_CONTIG_FREE(cy_session_ctx);
	QAT_PHYS_CONTIG_FREE(flat_src_buf_array);
	QAT_PHYS_CONTIG_FREE(flat_dst_buf_array);

	return (status);
}

module_param(zfs_qat_disable, int, 0644);
MODULE_PARM_DESC(zfs_qat_disable, "Disable QAT acceleration");

#endif
