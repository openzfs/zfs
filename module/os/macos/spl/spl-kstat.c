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

/*
 *
 * Copyright (C) 2013 Jorgen Lundman <lundman@lundman.net>
 * Copyright (C) 2014 Brendon Humphrey <brendon.humphrey@mac.com>
 *
 */

/*
 * Provides an implementation of kstat that is backed by OSX sysctls.
 */

#include <sys/kstat.h>
#include <sys/debug.h>
#include <sys/thread.h>
#include <sys/cmn_err.h>
#include <sys/time.h>
#include <sys/sysctl.h>
#include <sys/sbuf.h>

/*
 * We need to get dynamically allocated memory from the kernel allocator
 * (Our needs are small, we wont blow the zone_map).
 */
void *IOMalloc(vm_size_t size);
void IOFree(void *address, vm_size_t size);

void *IOMallocAligned(vm_size_t size, vm_offset_t alignment);
void IOFreeAligned(void *address, vm_size_t size);

/*
 * Statically declared toplevel OID that all kstats
 * will hang off.
 */
struct sysctl_oid_list sysctl__kstat_children;
SYSCTL_DECL(_kstat);
SYSCTL_NODE(, OID_AUTO, kstat, CTLFLAG_RW, 0, "kstat tree");

/*
 * Sysctl node tree structure.
 *
 * These are wired into the OSX sysctl structure
 * and also stored a list/tree/whatever for easy
 * location and destruction at shutdown time.
 */
typedef struct sysctl_tree_node {
	char			tn_kstat_name[KSTAT_STRLEN + 1];
	struct sysctl_oid_list	tn_children;
	struct sysctl_oid	tn_oid;
	struct sysctl_tree_node	*tn_next;
} sysctl_tree_node_t;

/*
 * Each named kstats consists of one or more named
 * fields which are implemented as OIDs parented
 * off the kstat OID.
 *
 * To implement the kstat interface, we need to be able
 * to call the update() function on the kstat to
 * allow the owner to populate the kstat values from
 * internal data.
 *
 * To do this we need the address of the kstat_named_t
 * which contains the data value, and the owning kstat_t.
 *
 * OIDs allow a single void* user argument, so we will
 * use a structure that contains both values and
 * point to that.
 */
typedef struct sysctl_leaf {
	kstat_t		*l_ksp;
	kstat_named_t	*l_named;
	struct sysctl_oid l_oid;	/* kstats are backed w/sysctl */
	char	l_name[KSTAT_STRLEN + 1]; /* Name of the related sysctl */
	int	l_oid_registered;	/* !0 = registered */
} sysctl_leaf_t;

/*
 * Extended kstat structure -- for internal use only.
 */
typedef struct ekstat {
	kstat_t			e_ks;		/* the kstat itself */
	size_t			e_size;		/* total allocation size */
	kthread_t		*e_owner;	/* thread holding this kstat */
	kcondvar_t		e_cv;		/* wait for owner == NULL */
	/* contains the named values from the kstat */
	struct sysctl_oid_list	e_children;
	struct sysctl_oid	e_oid;		/* the kstat is itself an OID */
	/* array of OIDs that implement the children */
	sysctl_leaf_t		*e_vals;
	uint64_t		e_num_vals;	/* size of e_vals array */
} ekstat_t;

struct sysctl_tree_node		*tree_nodes = 0;
struct sysctl_oid 		*e_sysctl = 0;

/* sbuf_new() and family does exist in XNU, but Apple wont let us call them */
#define	M_SBUF		105 /* string buffers */
#define	SBMALLOC(size)	_MALLOC(size, M_SBUF, M_WAITOK)
#define	SBFREE(buf)	FREE(buf, M_SBUF)

#define	SBUF_SETFLAG(s, f)	do { (s)->s_flags |= (f); } while (0)
#define	SBUF_CLEARFLAG(s, f)	do { (s)->s_flags &= ~(f); } while (0)
#define	SBUF_ISDYNAMIC(s)	((s)->s_flags & SBUF_DYNAMIC)
#define	SBUF_ISDYNSTRUCT(s)	((s)->s_flags & SBUF_DYNSTRUCT)
#define	SBUF_HASOVERFLOWED(s)	((s)->s_flags & SBUF_OVERFLOWED)
#define	SBUF_HASROOM(s)		((s)->s_len < (s)->s_size - 1)
#define	SBUF_FREESPACE(s)	((s)->s_size - (s)->s_len - 1)
#define	SBUF_CANEXTEND(s)	((s)->s_flags & SBUF_AUTOEXTEND)
#define	SBUF_ISFINISHED(s)	((s)->s_flags & SBUF_FINISHED)

#define	SBUF_MINEXTENDSIZE	16	/* Should be power of 2. */
#define	SBUF_MAXEXTENDSIZE	PAGE_SIZE
#define	SBUF_MAXEXTENDINCR	PAGE_SIZE

#define	SBUF_INCLUDENUL 0x00000002 /* FBSD: nulterm byte is counted in len */
#define	SBUF_NULINCLUDED(s) ((s)->s_flags & SBUF_INCLUDENUL)

void
sbuf_finish(struct sbuf *s)
{
	s->s_buf[s->s_len] = '\0';
	if (SBUF_NULINCLUDED(s))
		s->s_len++;

	SBUF_CLEARFLAG(s, SBUF_OVERFLOWED);
	SBUF_SETFLAG(s, SBUF_FINISHED);
}

char *
sbuf_data(struct sbuf *s)
{
	return (s->s_buf);
}

int
sbuf_len(struct sbuf *s)
{
	if (SBUF_HASOVERFLOWED(s)) {
		return (-1);
	}
	/* If finished, nulterm is already in len, else add one. */
	if (SBUF_NULINCLUDED(s) && !SBUF_ISFINISHED(s))
		return (s->s_len + 1);
	return (s->s_len);
}

void
sbuf_delete(struct sbuf *s)
{
	int isdyn;
	if (SBUF_ISDYNAMIC(s)) {
		SBFREE(s->s_buf);
	}
	isdyn = SBUF_ISDYNSTRUCT(s);
	memset(s, 0, sizeof (*s));
	if (isdyn) {
		SBFREE(s);
	}
}

static int
sbuf_extendsize(int size)
{
	int newsize;

	newsize = SBUF_MINEXTENDSIZE;
	while (newsize < size) {
		if (newsize < (int)SBUF_MAXEXTENDSIZE) {
			newsize *= 2;
		} else {
			newsize += SBUF_MAXEXTENDINCR;
		}
	}

	return (newsize);
}

static int
sbuf_extend(struct sbuf *s, int addlen)
{
	char *newbuf;
	int newsize;

	if (!SBUF_CANEXTEND(s)) {
		return (-1);
	}

	newsize = sbuf_extendsize(s->s_size + addlen);
	newbuf = (char *)SBMALLOC(newsize);
	if (newbuf == NULL) {
		return (-1);
	}
	memcpy(newbuf, s->s_buf, s->s_size);
	if (SBUF_ISDYNAMIC(s)) {
		SBFREE(s->s_buf);
	} else {
		SBUF_SETFLAG(s, SBUF_DYNAMIC);
	}
	s->s_buf = newbuf;
	s->s_size = newsize;
	return (0);
}

struct sbuf *
sbuf_new(struct sbuf *s, char *buf, int length, int flags)
{
	flags &= SBUF_USRFLAGMSK;
	if (s == NULL) {
		s = (struct sbuf *)SBMALLOC(sizeof (*s));
		if (s == NULL) {
			return (NULL);
		}
		memset(s, 0, sizeof (*s));
		s->s_flags = flags;
		SBUF_SETFLAG(s, SBUF_DYNSTRUCT);
	} else {
		memset(s, 0, sizeof (*s));
		s->s_flags = flags;
	}
	s->s_size = length;
	if (buf) {
		s->s_buf = buf;
		return (s);
	}
	if (flags & SBUF_AUTOEXTEND) {
		s->s_size = sbuf_extendsize(s->s_size);
	}
	s->s_buf = (char *)SBMALLOC(s->s_size);
	if (s->s_buf == NULL) {
		if (SBUF_ISDYNSTRUCT(s)) {
			SBFREE(s);
		}
		return (NULL);
	}
	SBUF_SETFLAG(s, SBUF_DYNAMIC);
	return (s);
}

int
sbuf_vprintf(struct sbuf *s, const char *fmt, va_list ap)
{
	__builtin_va_list ap_copy; /* XXX tduffy - blame on him */
	int len;

	if (SBUF_HASOVERFLOWED(s)) {
		return (-1);
	}

	do {
		va_copy(ap_copy, ap);
		len = vsnprintf(&s->s_buf[s->s_len], SBUF_FREESPACE(s) + 1,
		    fmt, ap_copy);
		va_end(ap_copy);
	} while (len > SBUF_FREESPACE(s) &&
	    sbuf_extend(s, len - SBUF_FREESPACE(s)) == 0);
	s->s_len += min(len, SBUF_FREESPACE(s));
	if (!SBUF_HASROOM(s) && !SBUF_CANEXTEND(s)) {
		SBUF_SETFLAG(s, SBUF_OVERFLOWED);
	}

	if (SBUF_HASOVERFLOWED(s)) {
		return (-1);
	}
	return (0);
}

int
sbuf_printf(struct sbuf *s, const char *fmt, ...)
{
	va_list ap;
	int result;

	va_start(ap, fmt);
	result = sbuf_vprintf(s, fmt, ap);
	va_end(ap);
	return (result);
}

static void
kstat_set_string(char *dst, const char *src)
{
	memset(dst, 0, KSTAT_STRLEN);
	(void) strlcpy(dst, src, KSTAT_STRLEN);
}

static struct sysctl_oid *
get_oid_with_name(struct sysctl_oid_list *list, char *name)
{
	struct sysctl_oid *oidp;

	SLIST_FOREACH(oidp, list, oid_link) {
		if (strcmp(name, oidp->oid_name) == 0) {
			return (oidp);
		}
	}

	return (0);
}

static void
init_oid_tree_node(struct sysctl_oid_list *parent, char *name,
    sysctl_tree_node_t *node)
{
	strlcpy(node->tn_kstat_name, name, KSTAT_STRLEN);

	node->tn_oid.oid_parent = parent;
	node->tn_oid.oid_link.sle_next = 0;
	node->tn_oid.oid_number = OID_AUTO;
	node->tn_oid.oid_arg2 = 0;
	node->tn_oid.oid_name = &node->tn_kstat_name[0];
	node->tn_oid.oid_descr = "";
	node->tn_oid.oid_version = SYSCTL_OID_VERSION;
	node->tn_oid.oid_refcnt = 0;
	node->tn_oid.oid_handler = 0;
	node->tn_oid.oid_kind = CTLTYPE_NODE|CTLFLAG_RW|CTLFLAG_OID2;
	node->tn_oid.oid_fmt = "N";
	node->tn_oid.oid_arg1 = (void*)(&node->tn_children);

	sysctl_register_oid(&node->tn_oid);

	node->tn_next = tree_nodes;
	tree_nodes = node;
}

static struct sysctl_oid_list *
get_kstat_parent(struct sysctl_oid_list *root, char *module_name,
    char *class_name)
{
	struct sysctl_oid *the_module = 0;
	struct sysctl_oid *the_class = 0;
	sysctl_tree_node_t *new_node = 0;
	struct sysctl_oid_list *container = root;

	/*
	 * Locate/create the module
	 */
	the_module = get_oid_with_name(root, module_name);

	if (!the_module) {
		new_node = IOMalloc(sizeof (sysctl_tree_node_t));
		memset(new_node, 0, sizeof (sysctl_tree_node_t));
		init_oid_tree_node(root, module_name, new_node);
		the_module = &new_node->tn_oid;
	}

	/*
	 * Locate/create the class
	 */
	container = the_module->oid_arg1;
	the_class = get_oid_with_name(container, class_name);

	if (!the_class) {
		new_node = IOMalloc(sizeof (sysctl_tree_node_t));
		memset(new_node, 0, sizeof (sysctl_tree_node_t));
		init_oid_tree_node(container, class_name, new_node);
		the_class = &new_node->tn_oid;
	}

	container = the_class->oid_arg1;
	return (container);
}

struct sbuf *
sbuf_new_for_sysctl(struct sbuf *s, char *buf, int length,
    struct sysctl_req *req)
{
	/* Supply a default buffer size if none given. */
	if (buf == NULL && length == 0)
		length = 64;
	s = sbuf_new(s, buf, length, SBUF_FIXEDLEN | SBUF_INCLUDENUL);
	/* sbuf_set_drain(s, sbuf_sysctl_drain, req); */
	return (s);
}

static int
kstat_default_update(kstat_t *ksp, int rw)
{
	ASSERT(ksp != NULL);

	if (rw == KSTAT_WRITE)
		return (EACCES);

	return (0);
}

static int
kstat_resize_raw(kstat_t *ksp)
{
	if (ksp->ks_raw_bufsize == KSTAT_RAW_MAX)
		return (ENOMEM);

	IOFree(ksp->ks_raw_buf, ksp->ks_raw_bufsize);
	ksp->ks_raw_bufsize = MIN(ksp->ks_raw_bufsize * 2, KSTAT_RAW_MAX);
	ksp->ks_raw_buf = IOMalloc(ksp->ks_raw_bufsize);

	return (0);
}

static void *
kstat_raw_default_addr(kstat_t *ksp, loff_t n)
{
	if (n == 0)
		return (ksp->ks_data);
	return (NULL);
}

#define	HD_COLUMN_MASK	0xff
#define	HD_DELIM_MASK	0xff00
#define	HD_OMIT_COUNT	(1 << 16)
#define	HD_OMIT_HEX	(1 << 17)
#define	HD_OMIT_CHARS	(1 << 18)

void
sbuf_hexdump(struct sbuf *sb, const void *ptr, int length, const char *hdr,
    int flags)
{
	int i, j, k;
	int cols;
	const unsigned char *cp;
	char delim;

	if ((flags & HD_DELIM_MASK) != 0)
		delim = (flags & HD_DELIM_MASK) >> 8;
	else
		delim = ' ';

	if ((flags & HD_COLUMN_MASK) != 0)
		cols = flags & HD_COLUMN_MASK;
	else
		cols = 16;

	cp = ptr;
	for (i = 0; i < length; i += cols) {
		if (hdr != NULL)
			sbuf_printf(sb, "%s", hdr);

		if ((flags & HD_OMIT_COUNT) == 0)
			sbuf_printf(sb, "%04x  ", i);

		if ((flags & HD_OMIT_HEX) == 0) {
			for (j = 0; j < cols; j++) {
				k = i + j;
				if (k < length)
					sbuf_printf(sb, "%c%02x", delim, cp[k]);
				else
					sbuf_printf(sb, "   ");
			}
		}

		if ((flags & HD_OMIT_CHARS) == 0) {
			sbuf_printf(sb, "  |");
			for (j = 0; j < cols; j++) {
				k = i + j;
				if (k >= length)
					sbuf_printf(sb, " ");
				else if (cp[k] >= ' ' && cp[k] <= '~')
					sbuf_printf(sb, "%c", cp[k]);
				else
					sbuf_printf(sb, ".");
			}
			sbuf_printf(sb, "|");
		}
		sbuf_printf(sb, "\n");
	}
}

static int
kstat_handle_raw SYSCTL_HANDLER_ARGS
{
	struct sbuf *sb;
	void *data;
	kstat_t *ksp = arg1;
	void *(*addr_op)(kstat_t *ksp, loff_t index);
	int n, has_header, rc = 0;

	/* Check if this RAW has 2 entries, the second for verbose */
	ekstat_t *e = (ekstat_t *)ksp;
	if (e->e_num_vals == 2) {
		sysctl_leaf_t *val = &e->e_vals[1];
		if (strncmp("verbose", val->l_name, KSTAT_STRLEN) == 0) {
			int verbose = 0;
			if (val->l_oid.oid_arg1 != NULL)
				verbose = *((int *)val->l_oid.oid_arg1);
			if (verbose == 0)
				return (0);
		}
	}


	sb = sbuf_new(NULL, NULL, 0, SBUF_AUTOEXTEND);
	if (sb == NULL)
		return (ENOMEM);

	if (ksp->ks_raw_ops.addr)
		addr_op = ksp->ks_raw_ops.addr;
	else
		addr_op = kstat_raw_default_addr;

	VERIFY3P(ksp->ks_lock, !=, NULL);
	mutex_enter(ksp->ks_lock);

	/* Update the aggsums before reading */
	(void) ksp->ks_update(ksp, KSTAT_READ);

	ksp->ks_raw_bufsize = PAGE_SIZE;
	ksp->ks_raw_buf = IOMallocAligned(PAGE_SIZE, PAGE_SIZE);

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
		if (rc == 0)
			sbuf_printf(sb, "\n%s", ksp->ks_raw_buf);
	}

	while ((data = addr_op(ksp, n)) != NULL) {
restart:
		if (ksp->ks_raw_ops.data) {
			rc = ksp->ks_raw_ops.data(ksp->ks_raw_buf,
			    ksp->ks_raw_bufsize, data);
			if (rc == ENOMEM && !kstat_resize_raw(ksp))
				goto restart;
			if (rc == 0)
				sbuf_printf(sb, "%s", ksp->ks_raw_buf);

		} else {
			ASSERT(ksp->ks_ndata == 1);
			sbuf_hexdump(sb, ksp->ks_data,
			    ksp->ks_data_size, NULL, 0);
		}
		n++;
	}
	IOFreeAligned(ksp->ks_raw_buf, PAGE_SIZE);
	mutex_exit(ksp->ks_lock);
	rc = SYSCTL_OUT(req, sbuf_data(sb), sbuf_len(sb));
	sbuf_delete(sb);
	return (rc);
}

static int
kstat_handle_io SYSCTL_HANDLER_ARGS
{
	struct sbuf *sb;
	kstat_t *ksp = arg1;
	kstat_io_t *kip = ksp->ks_data;
	int rc;

	sb = sbuf_new(NULL, NULL, 0, SBUF_AUTOEXTEND);
	if (sb == NULL)
		return (ENOMEM);
	/* Update the aggsums before reading */
	(void) ksp->ks_update(ksp, KSTAT_READ);

	/* though wlentime & friends are signed, they will never be negative */
	sbuf_printf(sb,
		    "%-8llu %-8llu %-8u %-8u %-8llu %-8llu "
		"%-8llu %-8llu %-8llu %-8llu %-8u %-8u\n",
		kip->nread, kip->nwritten,
		kip->reads, kip->writes,
		kip->wtime, kip->wlentime, kip->wlastupdate,
		kip->rtime, kip->rlentime, kip->rlastupdate,
		kip->wcnt,  kip->rcnt);
	sbuf_finish(sb);
	rc = SYSCTL_OUT(req, sbuf_data(sb), sbuf_len(sb));
	sbuf_delete(sb);
	return (rc);
}

static int
kstat_handle_i64 SYSCTL_HANDLER_ARGS
{
    int error = 0;
	sysctl_leaf_t *params = (sysctl_leaf_t *)(arg1);
	kstat_named_t *named = params->l_named;
	kstat_t *ksp  = params->l_ksp;
	kmutex_t *lock = ksp->ks_lock;
	int lock_needs_release = 0;

	if (lock && !MUTEX_NOT_HELD(lock)) {
		mutex_enter(lock);
		lock_needs_release = 1;
	}

	if (ksp->ks_update) {
		ksp->ks_update(ksp, KSTAT_READ);
	}

	if (!error && req->newptr) {
		/*
		 * Write request - first read add current values for the kstat
		 * (remember that is sysctl is likely only one of many
		 *  values that make up the kstat).
		 */

		/* Copy the new value from user space */
		(void) copyin(req->newptr, &named->value.i64,
		    sizeof (named->value.i64));

		/* and invoke the update operation */
		if (ksp->ks_update) {
			error = ksp->ks_update(ksp, KSTAT_WRITE);
		}
	} else {
		/*
		 * Read request
		 */
		error = SYSCTL_OUT(req, &named->value.i64, sizeof (int64_t));
	}

	if (lock_needs_release) {
		mutex_exit(lock);
	}

	return (error);
}

static int
kstat_handle_ui64 SYSCTL_HANDLER_ARGS
{
	int error = 0;
	sysctl_leaf_t *params = (sysctl_leaf_t *)(arg1);
	kstat_named_t *named = params->l_named;
	kstat_t *ksp  = params->l_ksp;
	kmutex_t *lock = ksp->ks_lock;
	int lock_needs_release = 0;

	if (lock && !MUTEX_NOT_HELD(lock)) {
		mutex_enter(lock);
		lock_needs_release = 1;
	}

	if (ksp->ks_update) {
		ksp->ks_update(ksp, KSTAT_READ);
	}

	if (!error && req->newptr) {
		/*
		 * Write request - first read add current values for the kstat
		 * (remember that is sysctl is likely only one of many
		 *  values that make up the kstat).
		 */

		/* Copy the new value from user space */
		(void) copyin(req->newptr, &named->value.ui64,
		    sizeof (named->value.ui64));

		/* and invoke the update operation */
		if (ksp->ks_update) {
			error = ksp->ks_update(ksp, KSTAT_WRITE);
		}
	} else {
		/*
		 * Read request
		 */
		error = SYSCTL_OUT(req, &named->value.ui64, sizeof (uint64_t));
	}

	if (lock_needs_release) {
		mutex_exit(lock);
	}

    return (error);
}

static int
kstat_handle_string SYSCTL_HANDLER_ARGS
{
	int error = 0;
	sysctl_leaf_t *params = (sysctl_leaf_t *)(arg1);
	kstat_named_t *named = params->l_named;
	kstat_t *ksp  = params->l_ksp;
	kmutex_t *lock = ksp->ks_lock;
	int lock_needs_release = 0;

	if (lock && !MUTEX_NOT_HELD(lock)) {
		mutex_enter(lock);
		lock_needs_release = 1;
	}

	if (ksp->ks_update) {
		ksp->ks_update(ksp, KSTAT_READ);
	}

	if (!error && req->newptr) {
		char *inbuf = IOMalloc(256);

		error = SYSCTL_IN(req, inbuf, req->newlen);

		if (error == 0) {

			inbuf[req->newlen] = 0;

			/*
			 * Copy the new value from user space
			 * (copyin done by XNU)
			 */
			kstat_named_setstr(named, (const char *)inbuf);

			/* and invoke the update operation: last call out */
			if (ksp->ks_update)
				error = ksp->ks_update(ksp, KSTAT_WRITE);
		}

		IOFree(inbuf, 256);

	} else {

		error = SYSCTL_OUT(req, named->value.string.addr.ptr,
		    named->value.string.len);
	}

	if (lock_needs_release) {
		mutex_exit(lock);
	}

	return (error);
}

kstat_t *
kstat_create(const char *ks_module, int ks_instance, const char *ks_name,
    const char *ks_class, uchar_t ks_type, ulong_t ks_ndata, uchar_t ks_flags)
{
	kstat_t *ksp = 0;
	ekstat_t *e = 0;
	size_t size = 0;

	if (ks_class == NULL)
		ks_class = "misc";

	/*
	 * Allocate memory for the new kstat header.
	 */
	size = sizeof (ekstat_t);
	e = (ekstat_t *)IOMalloc(size);
	memset(e, 0, size);
	if (e == NULL) {
		cmn_err(CE_NOTE, "kstat_create('%s', %d, '%s'): "
		    "insufficient kernel memory",
		    ks_module, ks_instance, ks_name);
		return (NULL);
	}
	e->e_size = size;

	cv_init(&e->e_cv, NULL, CV_DEFAULT, NULL);

	/*
	 * Initialize as many fields as we can.  The caller may reset
	 * ks_lock, ks_update, ks_private, and ks_snapshot as necessary.
	 * Creators of virtual kstats may also reset ks_data.  It is
	 * also up to the caller to initialize the kstat data section,
	 * if necessary.  All initialization must be complete before
	 * calling kstat_install().
	 */
	ksp = &e->e_ks;

	ksp->ks_crtime		= gethrtime();
	kstat_set_string(ksp->ks_module, ks_module);
	ksp->ks_instance	= ks_instance;
	kstat_set_string(ksp->ks_name, ks_name);
	ksp->ks_type		= ks_type;
	kstat_set_string(ksp->ks_class, ks_class);
	ksp->ks_flags		= ks_flags | KSTAT_FLAG_INVALID;
	ksp->ks_snaptime	= ksp->ks_crtime;
	ksp->ks_update	= kstat_default_update;

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
			ASSERT(ks_ndata == 1);
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



	/*
	 * Initialise the sysctl that represents this kstat
	 */
	e->e_children.slh_first = 0;

	e->e_oid.oid_parent = get_kstat_parent(&sysctl__kstat_children,
	    ksp->ks_module, ksp->ks_class);
	e->e_oid.oid_link.sle_next = 0;
	e->e_oid.oid_number = OID_AUTO;
	e->e_oid.oid_arg2 = 0;
	e->e_oid.oid_name = ksp->ks_name;
	e->e_oid.oid_descr = "";
	e->e_oid.oid_version = SYSCTL_OID_VERSION;
	e->e_oid.oid_refcnt = 0;
	e->e_oid.oid_handler = 0;
	e->e_oid.oid_kind = CTLTYPE_NODE|CTLFLAG_RW|CTLFLAG_OID2;
	e->e_oid.oid_fmt = "N";
	e->e_oid.oid_arg1 = (void*)(&e->e_children);

	/* If VIRTUAL we allocate memory to store data */
	if (ks_flags & KSTAT_FLAG_VIRTUAL)
		ksp->ks_data    = NULL;
	else
		ksp->ks_data    = (void *)kmem_zalloc(
		    ksp->ks_data_size, KM_SLEEP);

	sysctl_register_oid(&e->e_oid);

	return (ksp);
}

void
kstat_install(kstat_t *ksp)
{
	ekstat_t *e = (ekstat_t *)ksp;
	kstat_named_t *named_base = 0;
	sysctl_leaf_t *vals_base = 0;
	sysctl_leaf_t *params = 0;
	int oid_permissions = CTLFLAG_RD;

	if (ksp->ks_type == KSTAT_TYPE_NAMED) {

		if (ksp->ks_flags & KSTAT_FLAG_WRITABLE) {
			oid_permissions |= CTLFLAG_RW;
		}

		// Create the leaf node OID objects
		e->e_vals = (sysctl_leaf_t *)IOMalloc(ksp->ks_ndata *
		    sizeof (sysctl_leaf_t));
		memset(e->e_vals, 0, ksp->ks_ndata * sizeof (sysctl_leaf_t));
		e->e_num_vals = ksp->ks_ndata;

		named_base = (kstat_named_t *)(ksp->ks_data);
		vals_base = e->e_vals;

		for (int i = 0; i < ksp->ks_ndata; i++) {
			int oid_valid = 1;

			kstat_named_t *named = &named_base[i];
			sysctl_leaf_t *val = &vals_base[i];

			// Perform basic initialisation of the sysctl.
			//
			// The sysctl: kstat.<module>.<class>.<name>.<data name>
			snprintf(val->l_name, KSTAT_STRLEN, "%s", named->name);

			val->l_oid.oid_parent = &e->e_children;
			val->l_oid.oid_link.sle_next = 0;
			val->l_oid.oid_number = OID_AUTO;
			val->l_oid.oid_arg2 = 0;
			val->l_oid.oid_name = val->l_name;
			val->l_oid.oid_descr = "";
			val->l_oid.oid_version = SYSCTL_OID_VERSION;
			val->l_oid.oid_refcnt = 0;

			// Based on the kstat type flags, provide location
			// of data item and associated type and handler
			// flags to the sysctl.
			switch (named->data_type) {
				case KSTAT_DATA_INT64:
					params = (sysctl_leaf_t *)IOMalloc(
					    sizeof (sysctl_leaf_t));
					params->l_named = named;
					params->l_ksp = ksp;

					val->l_oid.oid_handler =
					    kstat_handle_i64;
					val->l_oid.oid_kind = CTLTYPE_QUAD |
					    oid_permissions | CTLFLAG_OID2;
					val->l_oid.oid_fmt = "Q";
					val->l_oid.oid_arg1 = (void*)params;
					params = 0;
					break;
				case KSTAT_DATA_UINT64:
					params = (sysctl_leaf_t *)IOMalloc(
					    sizeof (sysctl_leaf_t));
					params->l_named = named;
					params->l_ksp = ksp;

					val->l_oid.oid_handler =
					    kstat_handle_ui64;
					val->l_oid.oid_kind = CTLTYPE_QUAD |
					    oid_permissions | CTLFLAG_OID2;
					val->l_oid.oid_fmt = "Q";
					val->l_oid.oid_arg1 = (void*)params;
					break;
				case KSTAT_DATA_INT32:
					val->l_oid.oid_handler =
					    sysctl_handle_int;
					val->l_oid.oid_kind = CTLTYPE_INT |
					    oid_permissions | CTLFLAG_OID2;
					val->l_oid.oid_fmt = "I";
					val->l_oid.oid_arg1 = &named->value.i32;
					break;
				case KSTAT_DATA_UINT32:
					val->l_oid.oid_handler =
					    sysctl_handle_int;
					val->l_oid.oid_kind = CTLTYPE_INT |
					    oid_permissions | CTLFLAG_OID2;
					val->l_oid.oid_fmt = "IU";
					val->l_oid.oid_arg1 =
					    &named->value.ui32;
					break;
				case KSTAT_DATA_LONG:
					val->l_oid.oid_handler =
					    sysctl_handle_long;
					val->l_oid.oid_kind = CTLTYPE_INT |
					    oid_permissions | CTLFLAG_OID2;
					val->l_oid.oid_fmt = "L";
					val->l_oid.oid_arg1 = &named->value.l;
					break;
				case KSTAT_DATA_ULONG:
					val->l_oid.oid_handler =
					    sysctl_handle_long;
					val->l_oid.oid_kind = CTLTYPE_INT |
					    oid_permissions | CTLFLAG_OID2;
					val->l_oid.oid_fmt = "L";
					val->l_oid.oid_arg1 = &named->value.ul;
					break;
				case KSTAT_DATA_STRING:
					params = (sysctl_leaf_t *)IOMalloc(
					    sizeof (sysctl_leaf_t));
					params->l_named = named;
					params->l_ksp = ksp;
					val->l_oid.oid_handler =
					    kstat_handle_string;
					val->l_oid.oid_kind = CTLTYPE_STRING |
					    oid_permissions | CTLFLAG_OID2;
					val->l_oid.oid_fmt = "S";
					val->l_oid.oid_arg1 = (void*)params;
					break;

				case KSTAT_DATA_CHAR:
				default:
					oid_valid = 0;
					break;
			}

			/*
			 * Finally publish the OID, provided that there were
			 * no issues initialising it.
			 */
			if (oid_valid) {
				sysctl_register_oid(&val->l_oid);
				val->l_oid_registered = 1;
			} else {
				val->l_oid_registered = 0;
			}
		}

	} else if (ksp->ks_type == KSTAT_TYPE_RAW) {

		e->e_vals = (sysctl_leaf_t *)
		    IOMalloc(sizeof (sysctl_leaf_t) * 2);
		memset(e->e_vals, 0, sizeof (sysctl_leaf_t));
		e->e_num_vals = 2;
		sysctl_leaf_t *val = e->e_vals;

		snprintf(val->l_name, KSTAT_STRLEN, "%s", ksp->ks_name);

		val->l_oid.oid_parent = &e->e_children;
		val->l_oid.oid_link.sle_next = 0;
		val->l_oid.oid_number = OID_AUTO;
		val->l_oid.oid_arg2 = 0;
		val->l_oid.oid_name = val->l_name;
		val->l_oid.oid_descr = "";
		val->l_oid.oid_version = SYSCTL_OID_VERSION;
		val->l_oid.oid_refcnt = 0;

		if (ksp->ks_raw_ops.data) {
			val->l_oid.oid_handler =
			    kstat_handle_raw;
			val->l_oid.oid_kind = CTLTYPE_STRING |
			    CTLFLAG_RD | CTLFLAG_OID2;
			val->l_oid.oid_fmt = "A";
			val->l_oid.oid_arg1 = (void *) ksp;
			sysctl_register_oid(&val->l_oid);
		} else {
			val->l_oid.oid_handler =
			    kstat_handle_raw;
			val->l_oid.oid_kind = CTLTYPE_OPAQUE |
			    CTLFLAG_RD | CTLFLAG_OID2;
			val->l_oid.oid_fmt = "";
			val->l_oid.oid_arg1 = (void *) ksp;
			sysctl_register_oid(&val->l_oid);
		}
		val->l_oid_registered = 1;

		// Add "verbose" leaf to 2nd node
		val++;

		snprintf(val->l_name, KSTAT_STRLEN, "verbose");

		val->l_oid.oid_parent = &e->e_children;
		val->l_oid.oid_link.sle_next = 0;
		val->l_oid.oid_number = OID_AUTO;
		val->l_oid.oid_arg2 = 0;
		val->l_oid.oid_name = val->l_name;
		val->l_oid.oid_descr = "";
		val->l_oid.oid_version = SYSCTL_OID_VERSION;
		val->l_oid.oid_refcnt = 0;

		val->l_oid.oid_handler =
		    sysctl_handle_int;
		val->l_oid.oid_kind = CTLTYPE_INT |
		    CTLFLAG_RW | CTLFLAG_OID2;
		val->l_oid.oid_fmt = "Q";
		/* Somewhat gross, using arg2 as the variable */
		val->l_oid.oid_arg1 = &val->l_oid.oid_arg2;
		sysctl_register_oid(&val->l_oid);
		val->l_oid_registered = 1;

	} else if (ksp->ks_type == KSTAT_TYPE_IO) {

		e->e_vals = (sysctl_leaf_t *)IOMalloc(sizeof (sysctl_leaf_t));
		memset(e->e_vals, 0, sizeof (sysctl_leaf_t));
		e->e_num_vals = 1;
		sysctl_leaf_t *val = e->e_vals;

		snprintf(val->l_name, KSTAT_STRLEN, "%s", ksp->ks_name);

		val->l_oid.oid_parent = &e->e_children;
		val->l_oid.oid_link.sle_next = 0;
		val->l_oid.oid_number = OID_AUTO;
		val->l_oid.oid_arg2 = 0;
		val->l_oid.oid_name = val->l_name;
		val->l_oid.oid_descr = "";
		val->l_oid.oid_version = SYSCTL_OID_VERSION;
		val->l_oid.oid_refcnt = 0;

		val->l_oid.oid_handler =
		    kstat_handle_io;
		val->l_oid.oid_kind = CTLTYPE_STRING |
		    CTLFLAG_RD | CTLFLAG_OID2;
		val->l_oid.oid_fmt = "A";
		val->l_oid.oid_arg1 = (void *) ksp;
		sysctl_register_oid(&val->l_oid);
		val->l_oid_registered = 1;
	}

	ksp->ks_flags &= ~KSTAT_FLAG_INVALID;
}

static void
remove_child_sysctls(ekstat_t *e)
{
	kstat_t *ksp = &e->e_ks;
	kstat_named_t *named_base = (kstat_named_t *)(ksp->ks_data);
	sysctl_leaf_t *vals_base = e->e_vals;

	for (int i = 0; i < ksp->ks_ndata; i++) {
		if (vals_base[i].l_oid_registered) {
			sysctl_unregister_oid(&vals_base[i].l_oid);
			vals_base[i].l_oid_registered = 0;
		}

		if (named_base[i].data_type == KSTAT_DATA_INT64 ||
		    named_base[i].data_type == KSTAT_DATA_UINT64 ||
		    named_base[i].data_type == KSTAT_DATA_STRING) {

			sysctl_leaf_t *leaf = (sysctl_leaf_t *)
			    vals_base[i].l_oid.oid_arg1;  /* params */
			IOFree(leaf, sizeof (sysctl_leaf_t));

			if (named_base[i].data_type == KSTAT_DATA_STRING) {
				void *data;
				int len;
				data = KSTAT_NAMED_STR_PTR(&named_base[i]);
				len = KSTAT_NAMED_STR_BUFLEN(&named_base[i]);
				// kstat_named_setstr(&named_base[i], NULL);
				if (data != NULL)
					dprintf(
					    "%s: unknown if %p:%d was freed.\n",
					    __func__, data, len);
			}
		}
	}
}

void
kstat_delete(kstat_t *ksp)
{
	ekstat_t *e = (ekstat_t *)ksp;
	kmutex_t *lock = ksp->ks_lock;
	int lock_needs_release = 0;

	// destroy the sysctl
	if (ksp->ks_type == KSTAT_TYPE_NAMED) {

		if (lock && MUTEX_NOT_HELD(lock)) {
			mutex_enter(lock);
			lock_needs_release = 1;
		}

		remove_child_sysctls(e);

		if (lock_needs_release) {
			mutex_exit(lock);
		}
	}

	sysctl_unregister_oid(&e->e_oid);

	if (e->e_vals) {
		IOFree(e->e_vals, sizeof (sysctl_leaf_t) * e->e_num_vals);
	}

	if (!(ksp->ks_flags & KSTAT_FLAG_VIRTUAL))
		kmem_free(ksp->ks_data, ksp->ks_data_size);

	ksp->ks_lock = NULL;
	mutex_destroy(&ksp->ks_private_lock);

	cv_destroy(&e->e_cv);
	IOFree(e, e->e_size);
}

void
kstat_named_setstr(kstat_named_t *knp, const char *src)
{
	void *data;
	int len;

	if (knp->data_type != KSTAT_DATA_STRING)
		panic("kstat_named_setstr('%p', '%p'): "
		    "named kstat is not of type KSTAT_DATA_STRING",
		    (void *)knp, (void *)src);

	data = KSTAT_NAMED_STR_PTR(knp);
	len = KSTAT_NAMED_STR_BUFLEN(knp);

	if (data != NULL && len > 0) {

		// If strings are the same, don't bother swapping them
		if (src != NULL &&
		    strcmp(src, data) == 0)
			return;

		IOFree(data, len);
		KSTAT_NAMED_STR_PTR(knp) = NULL;
		KSTAT_NAMED_STR_BUFLEN(knp) = 0;
	}

	if (src == NULL)
		return;

	len = strlen(src) + 1;

	data = IOMalloc(len);
	strlcpy(data, src, len);
	KSTAT_NAMED_STR_PTR(knp) = data;
	KSTAT_NAMED_STR_BUFLEN(knp) = len;
}

void
kstat_named_init(kstat_named_t *knp, const char *name, uchar_t data_type)
{
	kstat_set_string(knp->name, name);
	knp->data_type = data_type;

	if (data_type == KSTAT_DATA_STRING)
		kstat_named_setstr(knp, NULL);
}


void
kstat_waitq_enter(kstat_io_t *kiop)
{
}

void
kstat_waitq_exit(kstat_io_t *kiop)
{
}

void
kstat_runq_enter(kstat_io_t *kiop)
{
}

void
kstat_runq_exit(kstat_io_t *kiop)
{
}

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
	ksp->ks_raw_ops.data = data;
	ksp->ks_raw_ops.addr = addr;
}

void
spl_kstat_init()
{
	/*
	 * Create the kstat root OID
	 */
	sysctl_register_oid(&sysctl__kstat);
}

void
spl_kstat_fini()
{
	/*
	 * Destroy the kstat module/class/name tree
	 *
	 * Done in two passes, first unregisters all
	 * of the oids, second releases all the memory.
	 */

	sysctl_tree_node_t *iter = tree_nodes;
	while (iter) {
		sysctl_tree_node_t *tn = iter;
		iter = tn->tn_next;
		sysctl_unregister_oid(&tn->tn_oid);
	}

	while (tree_nodes) {
		sysctl_tree_node_t *tn = tree_nodes;
		tree_nodes = tn->tn_next;
		IOFree(tn, sizeof (sysctl_tree_node_t));
	}

	/*
	 * Destroy the root oid
	 */
	sysctl_unregister_oid(&sysctl__kstat);
}

// piece of shit
struct sysctl_oid_list *
spl_kstat_find_oid(char *module, char *class)
{
	struct sysctl_oid_list *container;

	container = get_kstat_parent(&sysctl__kstat_children,
	    module,
	    class);
	return (container);
}
