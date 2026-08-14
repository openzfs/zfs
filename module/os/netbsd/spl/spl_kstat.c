// SPDX-License-Identifier: BSD-2-Clause
/*
 * kstat for NetBSD: export named stats as sysctl(9) nodes under
 * kstat.<module-with-slashes>.<class>.<name>, matching FreeBSD.
 * Global stats land at kstat.zfs.misc.<name>; per-pool stats at
 * kstat.zfs.<pool>.misc.<name>.  RAW kstats (dbufs, dbgmsg, spa
 * state, …) are dumped as a growable string.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/sysctl.h>
#include <sys/kmem.h>
#include <string.h>
#include <sys/limits.h>
#include <sys/kstat.h>
#include <sys/condvar.h>
#include <sys/debug.h>

/*
 * Ceiling for the string a RAW kstat sysctl returns.
 * kstat_out_ensure() starts at PAGE_SIZE and doubles until the dump
 * fits; past this cap it returns ENOMEM.
 *
 * Not a NetBSD quantity.  sys/sysctl.h does not define it, and
 * KSTAT_RAW_MAX (128 KB) only caps the per-record scratch, matching
 * FreeBSD kstat_resize_raw().  16 MB is 128 times that scratch
 * (128 * 128 KB): room to concatenate many records without an
 * unbounded kmem_alloc.  Spelled as 128 * KSTAT_RAW_MAX so the two
 * caps stay tied together.
 *
 * No #ifndef.  If the kernel later defines KSTAT_RAW_OUT_MAX it will
 * not mean this, and the redefinition must fail the build.
 */
#define	KSTAT_RAW_OUT_MAX	(128 * KSTAT_RAW_MAX)

typedef struct kstat_sysctl_field {
	kstat_t	*sf_ksp;
	uint_t	sf_index;
} kstat_sysctl_field_t;

#ifndef	loff_t
typedef	off_t	loff_t;
#endif

static kmutex_t kstat_lock;
static kcondvar_t kstat_cv;
static int kstat_kid;
static struct sysctllog *kstat_sysctl_log;
static const struct sysctlnode *kstat_root;
static const struct sysctlnode *kstat_zfs;
static const struct sysctlnode *kstat_misc;
static int kstat_live;

static void	kstat_sysctl_ensure_tree(void);

/*
 * NetBSD sysctl(9) node names: [A-Za-z][A-Za-z0-9_-]{0,30}.
 * '.' is legal in a ZFS pool name (ZTS uses pool.<pid>) but EINVAL
 * here, and sysctl_createv prints every failure:
 *   sysctl_createv: sysctl_create(pool.9591) returned 22
 *
 * FreeBSD percent-encodes '.' as "%25".  '%' is also illegal on
 * NetBSD.  Do not mangle the name (collides, and the mapping is a
 * future exercise).  Skip the sysctl install when any path fragment
 * is not a valid sysctl name.  kstat.zfs.misc and pools whose names
 * already match the charset still export.
 */
static int
kstat_sysctl_name_ok(const char *s)
{
	size_t i;
	unsigned char c;

	if (s == NULL || s[0] == '\0')
		return (0);
	c = (unsigned char)s[0];
	if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')))
		return (0);
	for (i = 1; s[i] != '\0'; i++) {
		if (i >= SYSCTL_NAMELEN - 1)
			return (0);
		c = (unsigned char)s[i];
		if ((c >= '0' && c <= '9') ||
		    (c >= 'A' && c <= 'Z') ||
		    (c >= 'a' && c <= 'z') ||
		    c == '_' || c == '-')
			continue;
		return (0);
	}
	return (1);
}

static void *
kstat_raw_default_addr(kstat_t *ksp, loff_t n)
{
	if (n == 0)
		return (ksp->ks_data);
	return (NULL);
}

#ifdef SYSCTLFN_ARGS
static int
kstat_pin(kstat_t *ksp)
{
	mutex_enter(&kstat_lock);
	if (ksp == NULL || ksp->ks_magic != KS_MAGIC) {
		mutex_exit(&kstat_lock);
		return (EINVAL);
	}
	ksp->ks_readers++;
	mutex_exit(&kstat_lock);
	return (0);
}

static void
kstat_unpin(kstat_t *ksp)
{
	mutex_enter(&kstat_lock);
	VERIFY(ksp->ks_readers > 0);
	ksp->ks_readers--;
	cv_broadcast(&kstat_cv);
	mutex_exit(&kstat_lock);
}

static int
kstat_sysctl_named(SYSCTLFN_ARGS)
{
	kstat_sysctl_field_t *sf = rnode->sysctl_data;
	kstat_t *ksp;
	kstat_named_t *kn;
	uint64_t u64 = 0;
	int32_t i32 = 0;
	int error, ctype;
	struct sysctlnode node;

	if (sf == NULL)
		return (EINVAL);
	mutex_enter(&kstat_lock);
	ksp = sf->sf_ksp;
	if (ksp == NULL || ksp->ks_magic != KS_MAGIC) {
		mutex_exit(&kstat_lock);
		return (EINVAL);
	}
	ksp->ks_readers++;
	mutex_exit(&kstat_lock);
	if (ksp->ks_lock != NULL)
		mutex_enter(ksp->ks_lock);
	if (ksp->ks_update != NULL) {
		error = ksp->ks_update(ksp, KSTAT_READ);
		if (error != 0) {
			if (ksp->ks_lock != NULL)
				mutex_exit(ksp->ks_lock);
			kstat_unpin(ksp);
			return (error);
		}
	}
	if (ksp->ks_data == NULL || sf->sf_index >= ksp->ks_ndata) {
		if (ksp->ks_lock != NULL)
			mutex_exit(ksp->ks_lock);
		kstat_unpin(ksp);
		return (EINVAL);
	}
	kn = ksp->ks_data;
	kn += sf->sf_index;
	switch (kn->data_type) {
	case KSTAT_DATA_INT64:
		u64 = (uint64_t)kn->value.i64;
		ctype = CTLTYPE_QUAD;
		break;
	case KSTAT_DATA_UINT64:
		u64 = kn->value.ui64;
		ctype = CTLTYPE_QUAD;
		break;
	case KSTAT_DATA_LONG:
#if LONG_MAX == INT64_MAX
		u64 = (uint64_t)kn->value.l;
		ctype = CTLTYPE_QUAD;
#else
		i32 = (int32_t)kn->value.l;
		ctype = CTLTYPE_INT;
#endif
		break;
	case KSTAT_DATA_ULONG:
		u64 = (uint64_t)kn->value.ul;
		ctype = CTLTYPE_QUAD;
		break;
	case KSTAT_DATA_INT32:
		i32 = kn->value.i32;
		ctype = CTLTYPE_INT;
		break;
	case KSTAT_DATA_UINT32:
		u64 = (uint64_t)kn->value.ui32;
		ctype = CTLTYPE_QUAD;
		break;
	case KSTAT_DATA_STRING: {
		char *s = KSTAT_NAMED_STR_PTR(kn);
		uint32_t len = KSTAT_NAMED_STR_BUFLEN(kn);

		if (s == NULL || len == 0) {
			if (ksp->ks_lock != NULL)
				mutex_exit(ksp->ks_lock);
			kstat_unpin(ksp);
			return (EINVAL);
		}
		s[len - 1] = '\0';
		if (ksp->ks_lock != NULL)
			mutex_exit(ksp->ks_lock);
		node = *rnode;
		node.sysctl_data = s;
		error = sysctl_lookup(SYSCTLFN_CALL(&node));
		kstat_unpin(ksp);
		return (error);
	}
	default:
		if (ksp->ks_lock != NULL)
			mutex_exit(ksp->ks_lock);
		kstat_unpin(ksp);
		return (EINVAL);
	}
	if (ksp->ks_lock != NULL)
		mutex_exit(ksp->ks_lock);
	node = *rnode;
	if (ctype == CTLTYPE_QUAD)
		node.sysctl_data = &u64;
	else
		node.sysctl_data = &i32;
	error = sysctl_lookup(SYSCTLFN_CALL(&node));
	kstat_unpin(ksp);
	return (error);
}

/*
 * Grow the per-record scratch used by headers()/data(), same policy as
 * FreeBSD kstat_resize_raw(): double until KSTAT_RAW_MAX.
 */
static int
kstat_resize_raw(kstat_t *ksp)
{
	if (ksp->ks_raw_bufsize >= KSTAT_RAW_MAX)
		return (ENOMEM);
	if (ksp->ks_raw_buf != NULL)
		kmem_free(ksp->ks_raw_buf, ksp->ks_raw_bufsize);
	if (ksp->ks_raw_bufsize == 0)
		ksp->ks_raw_bufsize = PAGE_SIZE;
	else
		ksp->ks_raw_bufsize = MIN(ksp->ks_raw_bufsize * 2,
		    KSTAT_RAW_MAX);
	ksp->ks_raw_buf = kmem_alloc(ksp->ks_raw_bufsize, KM_SLEEP);
	return (0);
}

static int
kstat_out_ensure(char **buf, size_t *cap, size_t need)
{
	char *nbuf;
	size_t ncap;

	if (need <= *cap)
		return (0);
	if (need > KSTAT_RAW_OUT_MAX)
		return (ENOMEM);
	ncap = (*cap == 0) ? PAGE_SIZE : *cap;
	while (ncap < need) {
		if (ncap > KSTAT_RAW_OUT_MAX / 2) {
			ncap = KSTAT_RAW_OUT_MAX;
			break;
		}
		ncap *= 2;
	}
	if (ncap < need)
		return (ENOMEM);
	nbuf = kmem_alloc(ncap, KM_SLEEP);
	if (*buf != NULL) {
		(void) memcpy(nbuf, *buf, *cap);
		kmem_free(*buf, *cap);
	}
	*buf = nbuf;
	*cap = ncap;
	return (0);
}

static int
kstat_out_cat(char **buf, size_t *used, size_t *cap, const char *s)
{
	size_t len = strlen(s);

	if (kstat_out_ensure(buf, cap, *used + len + 1) != 0)
		return (ENOMEM);
	(void) memcpy(*buf + *used, s, len + 1);
	*used += len;
	return (0);
}

/*
 * RAW kstat dump.  Modelled on FreeBSD spl_kstat.c: grow the record
 * scratch when data() returns ENOMEM, emit a leading newline so
 * dbufstat(8) sees labels on line 3, and do not cap the whole dump at
 * PAGE_SIZE (dbufs of a 20MB file is tens of KB).
 */
static int
kstat_sysctl_raw(SYSCTLFN_ARGS)
{
	kstat_t *ksp = rnode->sysctl_data;
	char *out = NULL;
	size_t used = 0, cap = 0;
	int error, rc, n, has_header;
	struct sysctlnode node;
	void *(*addr_op)(kstat_t *, loff_t);
	void *data;

	error = kstat_pin(ksp);
	if (error != 0)
		return (error);

	if (ksp->ks_lock != NULL)
		mutex_enter(ksp->ks_lock);
	if (ksp->ks_update != NULL) {
		error = ksp->ks_update(ksp, KSTAT_READ);
		if (error != 0) {
			if (ksp->ks_lock != NULL)
				mutex_exit(ksp->ks_lock);
			kstat_unpin(ksp);
			return (error);
		}
	}

	addr_op = ksp->ks_raw_ops.addr != NULL ?
	    ksp->ks_raw_ops.addr : kstat_raw_default_addr;

	if (ksp->ks_raw_buf == NULL) {
		ksp->ks_raw_bufsize = PAGE_SIZE;
		ksp->ks_raw_buf = kmem_alloc(PAGE_SIZE, KM_SLEEP);
	}

	has_header = (ksp->ks_raw_ops.headers != NULL ||
	    ksp->ks_raw_ops.seq_headers != NULL);

restart_headers:
	rc = 0;
	if (ksp->ks_raw_ops.headers != NULL) {
		rc = ksp->ks_raw_ops.headers(ksp->ks_raw_buf,
		    ksp->ks_raw_bufsize);
	} else if (ksp->ks_raw_ops.seq_headers != NULL) {
		struct seq_file f;

		f.sf_buf = ksp->ks_raw_buf;
		f.sf_size = ksp->ks_raw_bufsize;
		rc = ksp->ks_raw_ops.seq_headers(&f);
	}
	if (has_header) {
		if (rc == ENOMEM && kstat_resize_raw(ksp) == 0)
			goto restart_headers;
		if (rc == 0) {
			if (kstat_out_cat(&out, &used, &cap, "\n") != 0 ||
			    kstat_out_cat(&out, &used, &cap,
			    ksp->ks_raw_buf) != 0) {
				error = ENOMEM;
				goto out;
			}
		}
	}

	n = 0;
	while ((data = addr_op(ksp, n)) != NULL) {
restart_data:
		if (ksp->ks_raw_ops.data != NULL) {
			rc = ksp->ks_raw_ops.data(ksp->ks_raw_buf,
			    ksp->ks_raw_bufsize, data);
			if (rc == ENOMEM && kstat_resize_raw(ksp) == 0)
				goto restart_data;
			if (rc == 0 && ksp->ks_raw_buf[0] != '\0') {
				if (kstat_out_cat(&out, &used, &cap,
				    ksp->ks_raw_buf) != 0) {
					error = ENOMEM;
					goto out;
				}
			}
		}
		n++;
	}
	error = 0;
out:
	if (ksp->ks_lock != NULL)
		mutex_exit(ksp->ks_lock);

	if (error != 0) {
		if (out != NULL)
			kmem_free(out, cap);
		kstat_unpin(ksp);
		return (error);
	}
	if (out == NULL) {
		out = kmem_alloc(1, KM_SLEEP);
		out[0] = '\0';
		cap = 1;
	}

	node = *rnode;
	node.sysctl_data = out;
	error = sysctl_lookup(SYSCTLFN_CALL(&node));
	kmem_free(out, cap);
	kstat_unpin(ksp);
	return (error);
}
#endif

void
spl_kstat_init(void)
{
	mutex_init(&kstat_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&kstat_cv, "kstat", CV_DEFAULT, NULL);
	kstat_kid = 1;
	kstat_sysctl_log = NULL;
	kstat_root = kstat_zfs = kstat_misc = NULL;
	kstat_sysctl_ensure_tree();
}

void
spl_kstat_fini(void)
{
	mutex_enter(&kstat_lock);
	if (kstat_live != 0)
		printf("ZFS: kstat_fini with %d live kstat(s)\n", kstat_live);
	mutex_exit(&kstat_lock);
	if (kstat_sysctl_log != NULL)
		sysctl_teardown(&kstat_sysctl_log);
	kstat_sysctl_log = NULL;
	kstat_root = kstat_zfs = kstat_misc = NULL;
	cv_destroy(&kstat_cv);
	mutex_destroy(&kstat_lock);
}

void
__kstat_set_raw_ops(kstat_t *ksp,
    int (*headers)(char *buf, size_t size),
    int (*data)(char *buf, size_t size, void *data),
    void *(*addr)(kstat_t *ksp, loff_t index))
{
	if (ksp == NULL)
		return;
	ksp->ks_raw_ops.headers = headers;
	ksp->ks_raw_ops.data = data;
	ksp->ks_raw_ops.addr = addr;
}

void
__kstat_set_seq_raw_ops(kstat_t *ksp,
    int (*headers)(struct seq_file *),
    int (*data)(char *buf, size_t size, void *data),
    void *(*addr)(kstat_t *ksp, loff_t index))
{
	if (ksp == NULL)
		return;
	ksp->ks_raw_ops.seq_headers = headers;
	ksp->ks_raw_ops.data = data;
	ksp->ks_raw_ops.addr = addr;
}

kstat_t *
__kstat_create(const char *ks_module, int ks_instance, const char *ks_name,
    const char *ks_class, uchar_t ks_type, uint_t ks_ndata, uchar_t ks_flags)
{
	kstat_t *ksp;

	ksp = kmem_zalloc(sizeof (*ksp), KM_SLEEP);
	if (ksp == NULL)
		return (NULL);
	ksp->ks_magic = KS_MAGIC;
	mutex_enter(&kstat_lock);
	ksp->ks_kid = kstat_kid++;
	mutex_exit(&kstat_lock);
	if (ks_module != NULL)
		strlcpy(ksp->ks_module, ks_module, sizeof (ksp->ks_module));
	if (ks_name != NULL)
		strlcpy(ksp->ks_name, ks_name, sizeof (ksp->ks_name));
	if (ks_class != NULL)
		strlcpy(ksp->ks_class, ks_class, sizeof (ksp->ks_class));
	else
		(void) strlcpy(ksp->ks_class, "misc", sizeof (ksp->ks_class));
	ksp->ks_instance = ks_instance;
	ksp->ks_type = ks_type;
	ksp->ks_flags = ks_flags;
	ksp->ks_ndata = ks_ndata;
	mutex_init(&ksp->ks_private_lock, NULL, MUTEX_DEFAULT, NULL);
	ksp->ks_lock = &ksp->ks_private_lock;
	mutex_enter(&kstat_lock);
	kstat_live++;
	mutex_exit(&kstat_lock);
	return (ksp);
}

static const struct sysctlnode *
kstat_sysctl_find_child(const struct sysctlnode *parent, const char *name)
{
	const struct sysctlnode *c;
	uint_t i, n;

	if (parent == NULL || name == NULL)
		return (NULL);
	n = parent->sysctl_clen;
	c = parent->sysctl_child;
	if (c == NULL || n == 0)
		return (NULL);
	for (i = 0; i < n; i++) {
		if (strcmp(c[i].sysctl_name, name) == 0)
			return (&c[i]);
	}
	return (NULL);
}

static void
kstat_sysctl_ensure_tree(void)
{
	mutex_enter(&kstat_lock);
	if (kstat_misc != NULL) {
		mutex_exit(&kstat_lock);
		return;
	}
	if (sysctl_createv(&kstat_sysctl_log, 0, NULL, &kstat_root,
	    0, CTLTYPE_NODE, "kstat",
	    SYSCTL_DESCR("kernel statistics"),
	    NULL, 0, NULL, 0, CTL_CREATE, CTL_EOL) != 0) {
		if (sysctl_createv(&kstat_sysctl_log, 0, NULL, &kstat_root,
		    0, CTLTYPE_NODE, "kstat",
		    SYSCTL_DESCR("kernel statistics"),
		    NULL, 0, NULL, 0, CTL_EOL) != 0) {
			printf("openzfs: kstat root missing\n");
			goto fail;
		}
	}
	if (sysctl_createv(&kstat_sysctl_log, 0, &kstat_root, &kstat_zfs,
	    0, CTLTYPE_NODE, "zfs",
	    SYSCTL_DESCR("ZFS kstats"),
	    NULL, 0, NULL, 0, CTL_CREATE, CTL_EOL) != 0) {
		kstat_zfs = kstat_sysctl_find_child(kstat_root, "zfs");
		if (kstat_zfs == NULL) {
			printf("openzfs: kstat.zfs missing\n");
			goto fail;
		}
	}
	if (sysctl_createv(&kstat_sysctl_log, 0, &kstat_zfs, &kstat_misc,
	    0, CTLTYPE_NODE, "misc",
	    SYSCTL_DESCR("ZFS misc kstats"),
	    NULL, 0, NULL, 0, CTL_CREATE, CTL_EOL) != 0) {
		kstat_misc = kstat_sysctl_find_child(kstat_zfs, "misc");
		if (kstat_misc == NULL) {
			printf("openzfs: kstat.zfs.misc missing\n");
			goto fail;
		}
	}
	mutex_exit(&kstat_lock);
	return;
fail:
	if (kstat_sysctl_log != NULL)
		sysctl_teardown(&kstat_sysctl_log);
	kstat_sysctl_log = NULL;
	kstat_root = kstat_zfs = kstat_misc = NULL;
	mutex_exit(&kstat_lock);
}

/*
 * Find or create a child node under parent on the global kstat log.
 * Intermediate pool nodes outlive any one kstat (teardown of a
 * per-kstat log must not yank kstat.zfs.<pool>).
 */
static const struct sysctlnode *
kstat_sysctl_child_get(const struct sysctlnode *parent, const char *name)
{
	const struct sysctlnode *child;
	int error;

	if (parent == NULL || name == NULL || name[0] == '\0')
		return (NULL);
	if (strlen(name) >= SYSCTL_NAMELEN)
		return (NULL);

	mutex_enter(&kstat_lock);
	child = kstat_sysctl_find_child(parent, name);
	mutex_exit(&kstat_lock);
	if (child != NULL)
		return (child);

	error = sysctl_createv(&kstat_sysctl_log, 0, &parent, &child,
	    0, CTLTYPE_NODE, name,
	    SYSCTL_DESCR("kstat"),
	    NULL, 0, NULL, 0, CTL_CREATE, CTL_EOL);
	if (error == 0)
		return (child);
	/*
	 * EEXIST: another kstat already made this pool/class node.
	 * EINVAL: duplicate create with CTL_CREATE.  Look it up.
	 */
	if (error != EEXIST && error != EINVAL)
		return (NULL);

	mutex_enter(&kstat_lock);
	child = kstat_sysctl_find_child(parent, name);
	mutex_exit(&kstat_lock);
	return (child);
}

/*
 * Walk kstat.<module-with-slashes>.<class>.  "zfs/testpool" + "misc"
 * becomes kstat.zfs.testpool.misc; "zfs" + "testpool" (procfs_list
 * reads/txgs/multihost) becomes kstat.zfs.testpool.
 */
static const struct sysctlnode *
kstat_sysctl_mkdirp(const char *module, const char *class)
{
	char buf[KSTAT_STRLEN];
	char *p, *frag;
	const struct sysctlnode *node;

	if (kstat_root == NULL)
		kstat_sysctl_ensure_tree();
	if (kstat_root == NULL)
		return (NULL);
	if (module == NULL || module[0] == '\0')
		return (NULL);
	if (class == NULL || class[0] == '\0')
		class = "misc";

	(void) strlcpy(buf, module, sizeof (buf));
	node = kstat_root;
	p = buf;
	while ((frag = strsep(&p, "/")) != NULL) {
		if (frag[0] == '\0')
			continue;
		if (!kstat_sysctl_name_ok(frag))
			return (NULL);
		node = kstat_sysctl_child_get(node, frag);
		if (node == NULL)
			return (NULL);
	}
	if (!kstat_sysctl_name_ok(class))
		return (NULL);
	return (kstat_sysctl_child_get(node, class));
}

void
__kstat_install(kstat_t *ksp)
{
	const struct sysctlnode *nnode;
	const struct sysctlnode *parent;
	const char *class;
	kstat_named_t *kn;
	uint_t i;

	if (ksp == NULL)
		return;
	class = ksp->ks_class[0] != '\0' ? ksp->ks_class : "misc";
	if (!kstat_sysctl_name_ok(ksp->ks_name))
		return;
	parent = kstat_sysctl_mkdirp(ksp->ks_module, class);
	if (parent == NULL)
		return;

	mutex_enter(&kstat_lock);
	if (ksp->ks_installing != 0 || ksp->ks_sysctl_ctx != NULL) {
		mutex_exit(&kstat_lock);
		return;
	}

#ifdef SYSCTLFN_ARGS
	if (ksp->ks_type == KSTAT_TYPE_RAW) {
		struct sysctllog *log = NULL;
		int error;

		ksp->ks_installing = 1;
		mutex_exit(&kstat_lock);
		/*
		 * Handler-backed nodes stash a cookie in sysctl_data.
		 * The sysctl_createv macro type-checks newp against
		 * CTLTYPE_*; call the function instead, as kmod_core.c
		 * does for the same reason.
		 */
		error = (sysctl_createv)(&log, 0, &parent, NULL,
		    CTLFLAG_READONLY, CTLTYPE_STRING, ksp->ks_name,
		    SYSCTL_DESCR("raw kstat"),
		    kstat_sysctl_raw, 0, ksp, 0, CTL_CREATE, CTL_EOL);
		if (error == EEXIST)
			error = (sysctl_createv)(&log, 0, &parent, NULL,
			    CTLFLAG_READONLY, CTLTYPE_STRING, ksp->ks_name,
			    SYSCTL_DESCR("raw kstat"),
			    kstat_sysctl_raw, 0, ksp, 0, CTL_EOL);
		mutex_enter(&kstat_lock);
		if (error != 0 || ksp->ks_magic != KS_MAGIC) {
			ksp->ks_installing = 0;
			cv_broadcast(&kstat_cv);
			mutex_exit(&kstat_lock);
			if (log != NULL)
				sysctl_teardown(&log);
			return;
		}
		ksp->ks_sysctl_ctx = log;
		ksp->ks_installing = 0;
		cv_broadcast(&kstat_cv);
		mutex_exit(&kstat_lock);
		return;
	}
#endif

	if (ksp->ks_type != KSTAT_TYPE_NAMED || ksp->ks_data == NULL) {
		mutex_exit(&kstat_lock);
		return;
	}
	ksp->ks_installing = 1;
	{
		struct sysctllog *log = NULL;
		kstat_sysctl_field_t *fields;
		int error = 0;
		uint_t ndata = ksp->ks_ndata;

		fields = kmem_zalloc(sizeof (*fields) * ndata, KM_SLEEP);
		if (fields == NULL) {
			ksp->ks_installing = 0;
			cv_broadcast(&kstat_cv);
			mutex_exit(&kstat_lock);
			return;
		}
		mutex_exit(&kstat_lock);
		if (sysctl_createv(&log, 0, &parent, &nnode,
		    0, CTLTYPE_NODE, ksp->ks_name,
		    SYSCTL_DESCR("kstat group"),
		    NULL, 0, NULL, 0, CTL_CREATE, CTL_EOL) != 0) {
			mutex_enter(&kstat_lock);
			nnode = kstat_sysctl_find_child(parent, ksp->ks_name);
			mutex_exit(&kstat_lock);
			if (nnode == NULL) {
				mutex_enter(&kstat_lock);
				ksp->ks_installing = 0;
				cv_broadcast(&kstat_cv);
				mutex_exit(&kstat_lock);
				if (log != NULL)
					sysctl_teardown(&log);
				kmem_free(fields, sizeof (*fields) * ndata);
				return;
			}
		}

		kn = ksp->ks_data;
		for (i = 0; i < ndata; i++) {
			int ctype;
			const char *nm = kn[i].name;

			switch (kn[i].data_type) {
			case KSTAT_DATA_INT64:
			case KSTAT_DATA_UINT64:
			case KSTAT_DATA_UINT32:
			case KSTAT_DATA_ULONG:
#if LONG_MAX == INT64_MAX
			case KSTAT_DATA_LONG:
#endif
				ctype = CTLTYPE_QUAD;
				break;
			case KSTAT_DATA_INT32:
#if LONG_MAX != INT64_MAX
			case KSTAT_DATA_LONG:
#endif
				ctype = CTLTYPE_INT;
				break;
			case KSTAT_DATA_STRING:
				ctype = CTLTYPE_STRING;
				break;
			default:
				continue;
			}
			if (nm == NULL || nm[0] == '\0')
				continue;
			if (!kstat_sysctl_name_ok(nm))
				continue;
			fields[i].sf_ksp = ksp;
			fields[i].sf_index = i;
			if (ctype == CTLTYPE_QUAD)
				error = (sysctl_createv)(&log, 0,
				    &nnode, NULL, CTLFLAG_READONLY,
				    CTLTYPE_QUAD, nm,
				    SYSCTL_DESCR("kstat value"),
				    kstat_sysctl_named, 0, &fields[i], 0,
				    CTL_CREATE, CTL_EOL);
			else if (ctype == CTLTYPE_STRING)
				error = (sysctl_createv)(&log, 0,
				    &nnode, NULL, CTLFLAG_READONLY,
				    CTLTYPE_STRING, nm,
				    SYSCTL_DESCR("kstat value"),
				    kstat_sysctl_named, 0, &fields[i], 0,
				    CTL_CREATE, CTL_EOL);
			else
				error = (sysctl_createv)(&log, 0,
				    &nnode, NULL, CTLFLAG_READONLY,
				    CTLTYPE_INT, nm,
				    SYSCTL_DESCR("kstat value"),
				    kstat_sysctl_named, 0, &fields[i], 0,
				    CTL_CREATE, CTL_EOL);
			if (error != 0 && error != EEXIST) {
				/* One bad leaf must not yank the group. */
				error = 0;
				continue;
			}
		}
		mutex_enter(&kstat_lock);
		if (ksp->ks_magic != KS_MAGIC) {
			ksp->ks_installing = 0;
			cv_broadcast(&kstat_cv);
			mutex_exit(&kstat_lock);
			if (log != NULL)
				sysctl_teardown(&log);
			kmem_free(fields, sizeof (*fields) * ndata);
			return;
		}
		ksp->ks_sysctl_ctx = log;
		ksp->ks_sysctl_root = fields;
		ksp->ks_installing = 0;
		cv_broadcast(&kstat_cv);
		mutex_exit(&kstat_lock);
		return;
	}
}

void
__kstat_delete(kstat_t *ksp)
{
	struct sysctllog *log;
	void *fields;
	uint_t ndata;

	if (ksp == NULL)
		return;
	mutex_enter(&kstat_lock);
	while (ksp->ks_installing != 0)
		cv_wait(&kstat_cv, &kstat_lock);
	ksp->ks_magic = 0;
	ksp->ks_flags |= KSTAT_FLAG_INVALID;
	log = ksp->ks_sysctl_ctx;
	fields = ksp->ks_sysctl_root;
	ndata = ksp->ks_ndata;
	ksp->ks_sysctl_ctx = NULL;
	ksp->ks_sysctl_root = NULL;
	mutex_exit(&kstat_lock);
	if (log != NULL)
		sysctl_teardown(&log);
	mutex_enter(&kstat_lock);
	while (ksp->ks_readers > 0)
		cv_wait(&kstat_cv, &kstat_lock);
	if (kstat_live > 0)
		kstat_live--;
	mutex_exit(&kstat_lock);
	if (fields != NULL)
		kmem_free(fields, sizeof (kstat_sysctl_field_t) * ndata);
	mutex_destroy(&ksp->ks_private_lock);
	if (ksp->ks_raw_buf != NULL)
		kmem_free(ksp->ks_raw_buf, ksp->ks_raw_bufsize);
	kmem_free(ksp, sizeof (*ksp));
}
