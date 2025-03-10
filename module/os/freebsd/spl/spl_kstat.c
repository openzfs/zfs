// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Links to Illumos.org for more information on kstat function:
 * [1] https://illumos.org/man/1M/kstat
 * [2] https://illumos.org/man/9f/kstat_create
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/kstat.h>
#include <sys/sbuf.h>
#include <sys/zone.h>

static MALLOC_DEFINE(M_KSTAT, "kstat_data", "Kernel statistics");

SYSCTL_ROOT_NODE(OID_AUTO, kstat, CTLFLAG_RW, 0, "Kernel statistics");

void
__kstat_set_raw_ops(kstat_t *ksp,
    int (*headers)(char *buf, size_t size),
    int (*data)(char *buf, size_t size, void *data),
    void *(*addr)(kstat_t *ksp, loff_t index))
{
	ksp->ks_raw_ops.headers = headers;
	ksp->ks_raw_ops.data    = data;
	ksp->ks_raw_ops.addr    = addr;
}

void
__kstat_set_seq_raw_ops(kstat_t *ksp,
    int (*headers)(struct seq_file *f),
    int (*data)(char *buf, size_t size, void *data),
    void *(*addr)(kstat_t *ksp, loff_t index))
{
	ksp->ks_raw_ops.seq_headers = headers;
	ksp->ks_raw_ops.data    = data;
	ksp->ks_raw_ops.addr    = addr;
}

static int
kstat_default_update(kstat_t *ksp, int rw)
{
	ASSERT3P(ksp, !=, NULL);

	if (rw == KSTAT_WRITE)
		return (EACCES);

	return (0);
}

static int
kstat_resize_raw(kstat_t *ksp)
{
	if (ksp->ks_raw_bufsize == KSTAT_RAW_MAX)
		return (ENOMEM);

	free(ksp->ks_raw_buf, M_TEMP);
	ksp->ks_raw_bufsize = MIN(ksp->ks_raw_bufsize * 2, KSTAT_RAW_MAX);
	ksp->ks_raw_buf = malloc(ksp->ks_raw_bufsize, M_TEMP, M_WAITOK);

	return (0);
}

static void *
kstat_raw_default_addr(kstat_t *ksp, loff_t n)
{
	if (n == 0)
		return (ksp->ks_data);
	return (NULL);
}

static int
kstat_sysctl(SYSCTL_HANDLER_ARGS)
{
	kstat_t *ksp = arg1;
	kstat_named_t *ksent;
	uint64_t val;

	ksent = ksp->ks_data;
	/* Select the correct element */
	ksent += arg2;
	/* Update the aggsums before reading */
	(void) ksp->ks_update(ksp, KSTAT_READ);
	val = ksent->value.ui64;

	return (sysctl_handle_64(oidp, &val, 0, req));
}

static int
kstat_sysctl_string(SYSCTL_HANDLER_ARGS)
{
	kstat_t *ksp = arg1;
	kstat_named_t *ksent = ksp->ks_data;
	char *val;
	uint32_t len = 0;

	/* Select the correct element */
	ksent += arg2;
	/* Update the aggsums before reading */
	(void) ksp->ks_update(ksp, KSTAT_READ);
	val = KSTAT_NAMED_STR_PTR(ksent);
	len = KSTAT_NAMED_STR_BUFLEN(ksent);
	val[len-1] = '\0';

	return (sysctl_handle_string(oidp, val, len, req));
}

static int
kstat_sysctl_dataset(SYSCTL_HANDLER_ARGS)
{
	kstat_t *ksp = arg1;
	kstat_named_t *ksent;
	kstat_named_t *ksent_ds;
	uint64_t val;
	char *ds_name;
	uint32_t ds_len = 0;

	ksent_ds = ksent = ksp->ks_data;
	ds_name = KSTAT_NAMED_STR_PTR(ksent_ds);
	ds_len = KSTAT_NAMED_STR_BUFLEN(ksent_ds);
	ds_name[ds_len-1] = '\0';

	if (!zone_dataset_visible(ds_name, NULL)) {
		return (EPERM);
	}

	/* Select the correct element */
	ksent += arg2;
	/* Update the aggsums before reading */
	(void) ksp->ks_update(ksp, KSTAT_READ);
	val = ksent->value.ui64;

	return (sysctl_handle_64(oidp, &val, 0, req));
}

static int
kstat_sysctl_dataset_string(SYSCTL_HANDLER_ARGS)
{
	kstat_t *ksp = arg1;
	kstat_named_t *ksent = ksp->ks_data;
	char *val;
	uint32_t len = 0;

	/* Select the correct element */
	ksent += arg2;
	val = KSTAT_NAMED_STR_PTR(ksent);
	len = KSTAT_NAMED_STR_BUFLEN(ksent);
	val[len-1] = '\0';

	if (!zone_dataset_visible(val, NULL)) {
		return (EPERM);
	}

	return (sysctl_handle_string(oidp, val, len, req));
}

static int
kstat_sysctl_io(SYSCTL_HANDLER_ARGS)
{
	struct sbuf sb;
	kstat_t *ksp = arg1;
	kstat_io_t *kip = ksp->ks_data;
	int rc;

	sbuf_new_for_sysctl(&sb, NULL, 0, req);

	/* Update the aggsums before reading */
	(void) ksp->ks_update(ksp, KSTAT_READ);

	/* though wlentime & friends are signed, they will never be negative */
	sbuf_printf(&sb,
	    "%-8llu %-8llu %-8u %-8u %-8llu %-8llu "
	    "%-8llu %-8llu %-8llu %-8llu %-8u %-8u\n",
	    kip->nread, kip->nwritten,
	    kip->reads, kip->writes,
	    kip->wtime, kip->wlentime, kip->wlastupdate,
	    kip->rtime, kip->rlentime, kip->rlastupdate,
	    kip->wcnt,  kip->rcnt);
	rc = sbuf_finish(&sb);
	sbuf_delete(&sb);
	return (rc);
}

static int
kstat_sysctl_raw(SYSCTL_HANDLER_ARGS)
{
	struct sbuf sb;
	void *data;
	kstat_t *ksp = arg1;
	void *(*addr_op)(kstat_t *ksp, loff_t index);
	int n, has_header, rc = 0;

	sbuf_new_for_sysctl(&sb, NULL, PAGE_SIZE, req);

	if (ksp->ks_raw_ops.addr)
		addr_op = ksp->ks_raw_ops.addr;
	else
		addr_op = kstat_raw_default_addr;

	mutex_enter(ksp->ks_lock);

	/* Update the aggsums before reading */
	(void) ksp->ks_update(ksp, KSTAT_READ);

	ksp->ks_raw_bufsize = PAGE_SIZE;
	ksp->ks_raw_buf = malloc(PAGE_SIZE, M_TEMP, M_WAITOK);

	n = 0;
	has_header = (ksp->ks_raw_ops.headers ||
	    ksp->ks_raw_ops.seq_headers);

restart_headers:
	if (ksp->ks_raw_ops.headers) {
		rc = ksp->ks_raw_ops.headers(
		    ksp->ks_raw_buf, ksp->ks_raw_bufsize);
	} else if (ksp->ks_raw_ops.seq_headers) {
		struct seq_file f;

		f.sf_buf = ksp->ks_raw_buf;
		f.sf_size = ksp->ks_raw_bufsize;
		rc = ksp->ks_raw_ops.seq_headers(&f);
	}
	if (has_header) {
		if (rc == ENOMEM && !kstat_resize_raw(ksp))
			goto restart_headers;
		if (rc == 0) {
			sbuf_cat(&sb, "\n");
			sbuf_cat(&sb, ksp->ks_raw_buf);
		}
	}

	while ((data = addr_op(ksp, n)) != NULL) {
restart:
		if (ksp->ks_raw_ops.data) {
			rc = ksp->ks_raw_ops.data(ksp->ks_raw_buf,
			    ksp->ks_raw_bufsize, data);
			if (rc == ENOMEM && !kstat_resize_raw(ksp))
				goto restart;
			if (rc == 0)
				sbuf_cat(&sb, ksp->ks_raw_buf);

		} else {
			ASSERT3U(ksp->ks_ndata, ==, 1);
			sbuf_hexdump(&sb, ksp->ks_data,
			    ksp->ks_data_size, NULL, 0);
		}
		n++;
	}
	free(ksp->ks_raw_buf, M_TEMP);
	mutex_exit(ksp->ks_lock);
	rc = sbuf_finish(&sb);
	sbuf_delete(&sb);
	return (rc);
}

kstat_t *
__kstat_create(const char *module, int instance, const char *name,
    const char *class, uchar_t ks_type, uint_t ks_ndata, uchar_t flags)
{
	char buf[KSTAT_STRLEN];
	struct sysctl_oid *root;
	kstat_t *ksp;
	char *pool;

	KASSERT(instance == 0, ("instance=%d", instance));
	if ((ks_type == KSTAT_TYPE_INTR) || (ks_type == KSTAT_TYPE_IO))
		ASSERT3U(ks_ndata, ==, 1);

	if (class == NULL)
		class = "misc";

	/*
	 * Allocate the main structure. We don't need to keep a copy of
	 * module in here, because it is only used for sysctl node creation
	 * done in this function.
	 */
	ksp = malloc(sizeof (*ksp), M_KSTAT, M_WAITOK|M_ZERO);

	ksp->ks_crtime = gethrtime();
	ksp->ks_snaptime = ksp->ks_crtime;
	ksp->ks_instance = instance;
	(void) strlcpy(ksp->ks_name, name, KSTAT_STRLEN);
	(void) strlcpy(ksp->ks_class, class, KSTAT_STRLEN);
	ksp->ks_type = ks_type;
	ksp->ks_flags = flags;
	ksp->ks_update = kstat_default_update;

	mutex_init(&ksp->ks_private_lock, NULL, MUTEX_DEFAULT, NULL);
	ksp->ks_lock = &ksp->ks_private_lock;

	switch (ksp->ks_type) {
	case KSTAT_TYPE_RAW:
		ksp->ks_ndata = 1;
		ksp->ks_data_size = ks_ndata;
		break;
	case KSTAT_TYPE_NAMED:
		ksp->ks_ndata = ks_ndata;
		ksp->ks_data_size = ks_ndata * sizeof (kstat_named_t);
		break;
	case KSTAT_TYPE_INTR:
		ksp->ks_ndata = ks_ndata;
		ksp->ks_data_size = ks_ndata * sizeof (kstat_intr_t);
		break;
	case KSTAT_TYPE_IO:
		ksp->ks_ndata = ks_ndata;
		ksp->ks_data_size = ks_ndata * sizeof (kstat_io_t);
		break;
	case KSTAT_TYPE_TIMER:
		ksp->ks_ndata = ks_ndata;
		ksp->ks_data_size = ks_ndata * sizeof (kstat_timer_t);
		break;
	default:
		panic("Undefined kstat type %d\n", ksp->ks_type);
	}

	if (ksp->ks_flags & KSTAT_FLAG_VIRTUAL)
		ksp->ks_data = NULL;
	else
		ksp->ks_data = kmem_zalloc(ksp->ks_data_size, KM_SLEEP);

	/*
	 * Some kstats use a module name like "zfs/poolname" to distinguish a
	 * set of kstats belonging to a specific pool.  Split on '/' to add an
	 * extra node for the pool name if needed.
	 */
	(void) strlcpy(buf, module, KSTAT_STRLEN);
	module = buf;
	pool = strchr(module, '/');
	if (pool != NULL)
		*pool++ = '\0';

	/*
	 * Create sysctl tree for those statistics:
	 *
	 *	kstat.<module>[.<pool>].<class>.<name>
	 */
	sysctl_ctx_init(&ksp->ks_sysctl_ctx);
	root = SYSCTL_ADD_NODE(&ksp->ks_sysctl_ctx,
	    SYSCTL_STATIC_CHILDREN(_kstat), OID_AUTO, module, CTLFLAG_RW, 0,
	    "");
	if (root == NULL) {
		printf("%s: Cannot create kstat.%s tree!\n", __func__, module);
		sysctl_ctx_free(&ksp->ks_sysctl_ctx);
		free(ksp, M_KSTAT);
		return (NULL);
	}
	if (pool != NULL) {
		root = SYSCTL_ADD_NODE(&ksp->ks_sysctl_ctx,
		    SYSCTL_CHILDREN(root), OID_AUTO, pool, CTLFLAG_RW, 0, "");
		if (root == NULL) {
			printf("%s: Cannot create kstat.%s.%s tree!\n",
			    __func__, module, pool);
			sysctl_ctx_free(&ksp->ks_sysctl_ctx);
			free(ksp, M_KSTAT);
			return (NULL);
		}
	}
	root = SYSCTL_ADD_NODE(&ksp->ks_sysctl_ctx, SYSCTL_CHILDREN(root),
	    OID_AUTO, class, CTLFLAG_RW, 0, "");
	if (root == NULL) {
		if (pool != NULL)
			printf("%s: Cannot create kstat.%s.%s.%s tree!\n",
			    __func__, module, pool, class);
		else
			printf("%s: Cannot create kstat.%s.%s tree!\n",
			    __func__, module, class);
		sysctl_ctx_free(&ksp->ks_sysctl_ctx);
		free(ksp, M_KSTAT);
		return (NULL);
	}
	if (ksp->ks_type == KSTAT_TYPE_NAMED) {
		root = SYSCTL_ADD_NODE(&ksp->ks_sysctl_ctx,
		    SYSCTL_CHILDREN(root),
		    OID_AUTO, name, CTLFLAG_RW, 0, "");
		if (root == NULL) {
			if (pool != NULL)
				printf("%s: Cannot create kstat.%s.%s.%s.%s "
				    "tree!\n", __func__, module, pool, class,
				    name);
			else
				printf("%s: Cannot create kstat.%s.%s.%s "
				    "tree!\n", __func__, module, class, name);
			sysctl_ctx_free(&ksp->ks_sysctl_ctx);
			free(ksp, M_KSTAT);
			return (NULL);
		}

	}
	ksp->ks_sysctl_root = root;

	return (ksp);
}

static void
kstat_install_named(kstat_t *ksp)
{
	kstat_named_t *ksent;
	char *namelast;
	int typelast;

	ksent = ksp->ks_data;

	VERIFY((ksp->ks_flags & KSTAT_FLAG_VIRTUAL) || ksent != NULL);

	typelast = 0;
	namelast = NULL;

	for (int i = 0; i < ksp->ks_ndata; i++, ksent++) {
		if (ksent->data_type != 0) {
			typelast = ksent->data_type;
			namelast = ksent->name;
		}
		switch (typelast) {
		case KSTAT_DATA_CHAR:
			/* Not Implemented */
			break;
		case KSTAT_DATA_INT32:
			SYSCTL_ADD_PROC(&ksp->ks_sysctl_ctx,
			    SYSCTL_CHILDREN(ksp->ks_sysctl_root),
			    OID_AUTO, namelast,
			    CTLTYPE_S32 | CTLFLAG_RD | CTLFLAG_MPSAFE,
			    ksp, i, kstat_sysctl, "I", namelast);
			break;
		case KSTAT_DATA_UINT32:
			SYSCTL_ADD_PROC(&ksp->ks_sysctl_ctx,
			    SYSCTL_CHILDREN(ksp->ks_sysctl_root),
			    OID_AUTO, namelast,
			    CTLTYPE_U32 | CTLFLAG_RD | CTLFLAG_MPSAFE,
			    ksp, i, kstat_sysctl, "IU", namelast);
			break;
		case KSTAT_DATA_INT64:
			SYSCTL_ADD_PROC(&ksp->ks_sysctl_ctx,
			    SYSCTL_CHILDREN(ksp->ks_sysctl_root),
			    OID_AUTO, namelast,
			    CTLTYPE_S64 | CTLFLAG_RD | CTLFLAG_MPSAFE,
			    ksp, i, kstat_sysctl, "Q", namelast);
			break;
		case KSTAT_DATA_UINT64:
			if (strcmp(ksp->ks_class, "dataset") == 0) {
				SYSCTL_ADD_PROC(&ksp->ks_sysctl_ctx,
				    SYSCTL_CHILDREN(ksp->ks_sysctl_root),
				    OID_AUTO, namelast,
				    CTLTYPE_U64 | CTLFLAG_RD | CTLFLAG_MPSAFE,
				    ksp, i, kstat_sysctl_dataset, "QU",
				    namelast);
			} else {
				SYSCTL_ADD_PROC(&ksp->ks_sysctl_ctx,
				    SYSCTL_CHILDREN(ksp->ks_sysctl_root),
				    OID_AUTO, namelast,
				    CTLTYPE_U64 | CTLFLAG_RD | CTLFLAG_MPSAFE,
				    ksp, i, kstat_sysctl, "QU", namelast);
			}
			break;
		case KSTAT_DATA_LONG:
			SYSCTL_ADD_PROC(&ksp->ks_sysctl_ctx,
			    SYSCTL_CHILDREN(ksp->ks_sysctl_root),
			    OID_AUTO, namelast,
			    CTLTYPE_LONG | CTLFLAG_RD | CTLFLAG_MPSAFE,
			    ksp, i, kstat_sysctl, "L", namelast);
			break;
		case KSTAT_DATA_ULONG:
			SYSCTL_ADD_PROC(&ksp->ks_sysctl_ctx,
			    SYSCTL_CHILDREN(ksp->ks_sysctl_root),
			    OID_AUTO, namelast,
			    CTLTYPE_ULONG | CTLFLAG_RD | CTLFLAG_MPSAFE,
			    ksp, i, kstat_sysctl, "LU", namelast);
			break;
		case KSTAT_DATA_STRING:
			if (strcmp(ksp->ks_class, "dataset") == 0) {
				SYSCTL_ADD_PROC(&ksp->ks_sysctl_ctx,
				    SYSCTL_CHILDREN(ksp->ks_sysctl_root),
				    OID_AUTO, namelast, CTLTYPE_STRING |
				    CTLFLAG_RD | CTLFLAG_MPSAFE,
				    ksp, i, kstat_sysctl_dataset_string, "A",
				    namelast);
			} else {
				SYSCTL_ADD_PROC(&ksp->ks_sysctl_ctx,
				    SYSCTL_CHILDREN(ksp->ks_sysctl_root),
				    OID_AUTO, namelast, CTLTYPE_STRING |
				    CTLFLAG_RD | CTLFLAG_MPSAFE,
				    ksp, i, kstat_sysctl_string, "A",
				    namelast);
			}
			break;
		default:
			panic("unsupported type: %d", typelast);
		}
	}
}

void
kstat_install(kstat_t *ksp)
{
	struct sysctl_oid *root;

	if (ksp->ks_ndata == UINT32_MAX)
		VERIFY3U(ksp->ks_type, ==, KSTAT_TYPE_RAW);

	switch (ksp->ks_type) {
	case KSTAT_TYPE_NAMED:
		return (kstat_install_named(ksp));
	case KSTAT_TYPE_RAW:
		if (ksp->ks_raw_ops.data) {
			root = SYSCTL_ADD_PROC(&ksp->ks_sysctl_ctx,
			    SYSCTL_CHILDREN(ksp->ks_sysctl_root),
			    OID_AUTO, ksp->ks_name, CTLTYPE_STRING | CTLFLAG_RD
			    | CTLFLAG_MPSAFE | CTLFLAG_SKIP,
			    ksp, 0, kstat_sysctl_raw, "A", ksp->ks_name);
		} else {
			root = SYSCTL_ADD_PROC(&ksp->ks_sysctl_ctx,
			    SYSCTL_CHILDREN(ksp->ks_sysctl_root),
			    OID_AUTO, ksp->ks_name, CTLTYPE_OPAQUE | CTLFLAG_RD
			    | CTLFLAG_MPSAFE | CTLFLAG_SKIP,
			    ksp, 0, kstat_sysctl_raw, "", ksp->ks_name);
		}
		break;
	case KSTAT_TYPE_IO:
		root = SYSCTL_ADD_PROC(&ksp->ks_sysctl_ctx,
		    SYSCTL_CHILDREN(ksp->ks_sysctl_root),
		    OID_AUTO, ksp->ks_name,
		    CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_MPSAFE,
		    ksp, 0, kstat_sysctl_io, "A", ksp->ks_name);
		break;
	case KSTAT_TYPE_TIMER:
	case KSTAT_TYPE_INTR:
	default:
		panic("unsupported kstat type %d\n", ksp->ks_type);
	}
	VERIFY3P(root, !=, NULL);
	ksp->ks_sysctl_root = root;
}

void
kstat_delete(kstat_t *ksp)
{

	sysctl_ctx_free(&ksp->ks_sysctl_ctx);
	ksp->ks_lock = NULL;
	mutex_destroy(&ksp->ks_private_lock);
	if (!(ksp->ks_flags & KSTAT_FLAG_VIRTUAL))
		kmem_free(ksp->ks_data, ksp->ks_data_size);
	free(ksp, M_KSTAT);
}
