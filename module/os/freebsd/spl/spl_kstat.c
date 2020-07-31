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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/kstat.h>

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

static int
kstat_default_update(kstat_t *ksp, int rw)
{
	ASSERT(ksp != NULL);

	if (rw == KSTAT_WRITE)
		return (EACCES);

	return (0);
}

kstat_t *
__kstat_create(const char *module, int instance, const char *name,
    const char *class, uchar_t ks_type, uint_t ks_ndata, uchar_t flags)
{
	struct sysctl_oid *root;
	kstat_t *ksp;

	KASSERT(instance == 0, ("instance=%d", instance));
	if ((ks_type == KSTAT_TYPE_INTR) || (ks_type == KSTAT_TYPE_IO))
		ASSERT(ks_ndata == 1);

	/*
	 * Allocate the main structure. We don't need to copy module/class/name
	 * stuff in here, because it is only used for sysctl node creation
	 * done in this function.
	 */
	ksp = malloc(sizeof (*ksp), M_KSTAT, M_WAITOK|M_ZERO);

	ksp->ks_crtime = gethrtime();
	ksp->ks_snaptime = ksp->ks_crtime;
	ksp->ks_instance = instance;
	strncpy(ksp->ks_name, name, KSTAT_STRLEN);
	strncpy(ksp->ks_class, class, KSTAT_STRLEN);
	ksp->ks_type = ks_type;
	ksp->ks_flags = flags;
	ksp->ks_update = kstat_default_update;

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

	if (ksp->ks_flags & KSTAT_FLAG_VIRTUAL) {
		ksp->ks_data = NULL;
	} else {
		ksp->ks_data = kmem_zalloc(ksp->ks_data_size, KM_SLEEP);
		if (ksp->ks_data == NULL) {
			kmem_free(ksp, sizeof (*ksp));
			ksp = NULL;
		}
	}
	/*
	 * Create sysctl tree for those statistics:
	 *
	 *	kstat.<module>.<class>.<name>.
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
	root = SYSCTL_ADD_NODE(&ksp->ks_sysctl_ctx, SYSCTL_CHILDREN(root),
	    OID_AUTO, class, CTLFLAG_RW, 0, "");
	if (root == NULL) {
		printf("%s: Cannot create kstat.%s.%s tree!\n", __func__,
		    module, class);
		sysctl_ctx_free(&ksp->ks_sysctl_ctx);
		free(ksp, M_KSTAT);
		return (NULL);
	}
	root = SYSCTL_ADD_NODE(&ksp->ks_sysctl_ctx, SYSCTL_CHILDREN(root),
	    OID_AUTO, name, CTLFLAG_RW, 0, "");
	if (root == NULL) {
		printf("%s: Cannot create kstat.%s.%s.%s tree!\n", __func__,
		    module, class, name);
		sysctl_ctx_free(&ksp->ks_sysctl_ctx);
		free(ksp, M_KSTAT);
		return (NULL);
	}
	ksp->ks_sysctl_root = root;

	return (ksp);
}

static int
kstat_sysctl(SYSCTL_HANDLER_ARGS)
{
	kstat_t *ksp = arg1;
	kstat_named_t *ksent = ksp->ks_data;
	uint64_t val;

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

void
kstat_install(kstat_t *ksp)
{
	kstat_named_t *ksent;
	char *namelast;
	int typelast;

	ksent = ksp->ks_data;
	if (ksp->ks_ndata == UINT32_MAX) {
#ifdef INVARIANTS
		printf("can't handle raw ops yet!!!\n");
#endif
		return;
	}
	if (ksent == NULL) {
		printf("%s ksp->ks_data == NULL!!!!\n", __func__);
		return;
	}
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
				    CTLTYPE_S32 | CTLFLAG_RD, ksp, i,
				    kstat_sysctl, "I", namelast);
				break;
			case KSTAT_DATA_UINT32:
				SYSCTL_ADD_PROC(&ksp->ks_sysctl_ctx,
				    SYSCTL_CHILDREN(ksp->ks_sysctl_root),
				    OID_AUTO, namelast,
				    CTLTYPE_U32 | CTLFLAG_RD, ksp, i,
				    kstat_sysctl, "IU", namelast);
				break;
			case KSTAT_DATA_INT64:
				SYSCTL_ADD_PROC(&ksp->ks_sysctl_ctx,
				    SYSCTL_CHILDREN(ksp->ks_sysctl_root),
				    OID_AUTO, namelast,
				    CTLTYPE_S64 | CTLFLAG_RD, ksp, i,
				    kstat_sysctl, "Q", namelast);
				break;
			case KSTAT_DATA_UINT64:
				SYSCTL_ADD_PROC(&ksp->ks_sysctl_ctx,
				    SYSCTL_CHILDREN(ksp->ks_sysctl_root),
				    OID_AUTO, namelast,
				    CTLTYPE_U64 | CTLFLAG_RD, ksp, i,
				    kstat_sysctl, "QU", namelast);
				break;
			case KSTAT_DATA_LONG:
				SYSCTL_ADD_PROC(&ksp->ks_sysctl_ctx,
				    SYSCTL_CHILDREN(ksp->ks_sysctl_root),
				    OID_AUTO, namelast,
				    CTLTYPE_LONG | CTLFLAG_RD, ksp, i,
				    kstat_sysctl, "L", namelast);
				break;
			case KSTAT_DATA_ULONG:
				SYSCTL_ADD_PROC(&ksp->ks_sysctl_ctx,
				    SYSCTL_CHILDREN(ksp->ks_sysctl_root),
				    OID_AUTO, namelast,
				    CTLTYPE_ULONG | CTLFLAG_RD, ksp, i,
				    kstat_sysctl, "LU", namelast);
				break;
			case KSTAT_DATA_STRING:
				SYSCTL_ADD_PROC(&ksp->ks_sysctl_ctx,
				    SYSCTL_CHILDREN(ksp->ks_sysctl_root),
				    OID_AUTO, namelast,
				    CTLTYPE_STRING | CTLFLAG_RD, ksp, i,
				    kstat_sysctl_string, "A", namelast);
				break;
			default:
				panic("unsupported type: %d", typelast);
		}

	}
}

void
kstat_delete(kstat_t *ksp)
{

	sysctl_ctx_free(&ksp->ks_sysctl_ctx);
	free(ksp, M_KSTAT);
}

void
kstat_waitq_enter(kstat_io_t *kiop)
{
	hrtime_t new, delta;
	ulong_t wcnt;

	new = gethrtime();
	delta = new - kiop->wlastupdate;
	kiop->wlastupdate = new;
	wcnt = kiop->wcnt++;
	if (wcnt != 0) {
		kiop->wlentime += delta * wcnt;
		kiop->wtime += delta;
	}
}

void
kstat_waitq_exit(kstat_io_t *kiop)
{
	hrtime_t new, delta;
	ulong_t wcnt;

	new = gethrtime();
	delta = new - kiop->wlastupdate;
	kiop->wlastupdate = new;
	wcnt = kiop->wcnt--;
	ASSERT((int)wcnt > 0);
	kiop->wlentime += delta * wcnt;
	kiop->wtime += delta;
}

void
kstat_runq_enter(kstat_io_t *kiop)
{
	hrtime_t new, delta;
	ulong_t rcnt;

	new = gethrtime();
	delta = new - kiop->rlastupdate;
	kiop->rlastupdate = new;
	rcnt = kiop->rcnt++;
	if (rcnt != 0) {
		kiop->rlentime += delta * rcnt;
		kiop->rtime += delta;
	}
}

void
kstat_runq_exit(kstat_io_t *kiop)
{
	hrtime_t new, delta;
	ulong_t rcnt;

	new = gethrtime();
	delta = new - kiop->rlastupdate;
	kiop->rlastupdate = new;
	rcnt = kiop->rcnt--;
	ASSERT((int)rcnt > 0);
	kiop->rlentime += delta * rcnt;
	kiop->rtime += delta;
}
