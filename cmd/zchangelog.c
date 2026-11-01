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
 *
 * Copyright (c) 2026 by HeonJe Lee
 */
/*
 * zchangelog - read a dataset's changelog metadata change stream.
 *
 *	zchangelog register   <dataset> <consumer> [op,op,...]
 *	zchangelog read       <dataset> [-c consumer] [-s seq] [-n count]
 *	                               [-w ms] [-f] [-p] [-a]
 *	zchangelog clear      <dataset> <consumer> <seq>
 *	zchangelog unregister <dataset> <consumer>
 *	zchangelog status     <dataset>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/nvpair.h>
#include <sys/fs/zfs.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_changelog.h>
#include <libzfs_core.h>

static int g_fd = -1;
static boolean_t jflag = B_FALSE;	/* -j: JSON lines output */

static const char *const cl_opname[CL_OP_NTYPES] = {
	[CL_OP_CREATE]		= "CREATE",
	[CL_OP_REMOVE]		= "REMOVE",
	[CL_OP_RENAME]		= "RENAME",
	[CL_OP_RMDIR]		= "RMDIR",
	[CL_OP_MKDIR]		= "MKDIR",
	[CL_OP_SETXATTR]	= "SETXATTR",
	[CL_OP_SETATTR]		= "SETATTR",
	[CL_OP_SYMLINK]		= "SYMLINK",
	[CL_OP_LINK]		= "LINK",
	[CL_OP_MKNOD]		= "MKNOD",
};

static const char *
opname(uint8_t op)
{
	if (op < CL_OP_NTYPES && cl_opname[op] != NULL)
		return (cl_opname[op]);
	return ("UNKNOWN");
}

static int
op_by_name(const char *s)
{
	for (int i = 1; i < CL_OP_NTYPES; i++) {
		if (cl_opname[i] != NULL && strcasecmp(s, cl_opname[i]) == 0)
			return (i);
	}
	return (-1);
}

/* Issue one changelog ioctl, growing the output buffer on ENOMEM. */
static int
cl_ioctl(unsigned long ioc, const char *dsname, nvlist_t *innvl,
    nvlist_t **outnvl)
{
	zfs_cmd_t zc = {{0}};
	char *packed = NULL;
	size_t size = 0;
	size_t dstsize = 64 * 1024;
	int err;

	(void) strlcpy(zc.zc_name, dsname, sizeof (zc.zc_name));
	if (innvl != NULL) {
		packed = fnvlist_pack(innvl, &size);
		zc.zc_nvlist_src = (uint64_t)(uintptr_t)packed;
		zc.zc_nvlist_src_size = size;
	}

	for (;;) {
		zc.zc_nvlist_dst_size = dstsize;
		zc.zc_nvlist_dst = (uint64_t)(uintptr_t)malloc(dstsize);
		if (zc.zc_nvlist_dst == 0) {
			err = ENOMEM;
			break;
		}
		if (lzc_ioctl_fd(g_fd, ioc, &zc) == 0) {
			err = 0;
			break;
		}
		err = errno;
		free((void *)(uintptr_t)zc.zc_nvlist_dst);
		zc.zc_nvlist_dst = 0;
		if (err != ENOMEM)
			break;
		/* The kernel left the size it needs in zc_nvlist_dst_size. */
		dstsize = zc.zc_nvlist_dst_size * 2;
	}

	if (err == 0 && outnvl != NULL) {
		*outnvl = fnvlist_unpack((char *)(uintptr_t)zc.zc_nvlist_dst,
		    zc.zc_nvlist_dst_size);
	}
	if (zc.zc_nvlist_dst != 0)
		free((void *)(uintptr_t)zc.zc_nvlist_dst);
	if (packed != NULL)
		fnvlist_pack_free(packed, size);
	return (err);
}

/* Resolve an object number to a path, the same way zfs diff does. */
static const char *
obj_to_path(const char *dsname, uint64_t obj, char *buf, size_t buflen)
{
	zfs_cmd_t zc = {{0}};

	if (obj == 0)
		return ("-");
	(void) strlcpy(zc.zc_name, dsname, sizeof (zc.zc_name));
	zc.zc_obj = obj;
	if (lzc_ioctl_fd(g_fd, ZFS_IOC_OBJ_TO_PATH, &zc) != 0)
		return ("-");
	(void) strlcpy(buf, zc.zc_value, buflen);
	return (buf);
}

/* Minimal base64 (RFC 4648) alphabet for machine-readable name output. */
static const char b64t[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void
b64(const char *in, size_t n, char *out)
{
	size_t i, o = 0;

	for (i = 0; i + 2 < n; i += 3) {
		out[o++] = b64t[(in[i] >> 2) & 0x3f];
		out[o++] = b64t[((in[i] & 0x3) << 4) | (in[i + 1] >> 4)];
		out[o++] = b64t[((in[i + 1] & 0xf) << 2) |
		    (in[i + 2] >> 6)];
		out[o++] = b64t[in[i + 2] & 0x3f];
	}
	if (i < n) {
		out[o++] = b64t[(in[i] >> 2) & 0x3f];
		if (i + 1 < n) {
			out[o++] = b64t[((in[i] & 0x3) << 4) |
			    (in[i + 1] >> 4)];
			out[o++] = b64t[(in[i + 1] & 0xf) << 2];
			out[o++] = '=';
		} else {
			out[o++] = '=';
			out[o++] = '=';
		}
	}
	out[o] = '\0';
}

static void
jrec(const changelog_rec_t *r)
{
	char nb[300], nb2[300];

	if (r->clr_flags & CL_F_RENAME2) {
		uint16_t olen = r->clr_namelen;

		b64(r->clr_name, olen, nb);
		b64(r->clr_name + olen + 1,
		    strlen(r->clr_name + olen + 1), nb2);
		(void) printf("{\"type\":\"rec\",\"seq\":%llu,\"txg\":%llu,"
		    "\"time\":%llu,\"op\":\"%s\",\"object\":%llu,"
		    "\"parent\":%llu,\"spare\":%llu,"
		    "\"name\":\"%s\",\"name2\":\"%s\"}\n",
		    (u_longlong_t)r->clr_seq, (u_longlong_t)r->clr_txg,
		    (u_longlong_t)r->clr_time, opname(r->clr_op),
		    (u_longlong_t)r->clr_object, (u_longlong_t)r->clr_parent,
		    (u_longlong_t)r->clr_spare, nb, nb2);
	} else {
		b64(r->clr_name, r->clr_namelen, nb);
		(void) printf("{\"type\":\"rec\",\"seq\":%llu,\"txg\":%llu,"
		    "\"time\":%llu,\"op\":\"%s\",\"object\":%llu,"
		    "\"parent\":%llu,\"name\":\"%s\",\"flags\":%u}\n",
		    (u_longlong_t)r->clr_seq, (u_longlong_t)r->clr_txg,
		    (u_longlong_t)r->clr_time, opname(r->clr_op),
		    (u_longlong_t)r->clr_object, (u_longlong_t)r->clr_parent,
		    nb, r->clr_flags);
	}
}

static void
print_rec(const char *dsname, const changelog_rec_t *r, boolean_t paths)
{
	char tbuf[32], pbuf[MAXPATHLEN];
	time_t t = (time_t)r->clr_time;
	struct tm tm;

	(void) localtime_r(&t, &tm);
	(void) strftime(tbuf, sizeof (tbuf), "%Y-%m-%d %H:%M:%S", &tm);

	(void) printf("%-10llu %-8llu %s %-12s %-8llu %-8llu ",
	    (u_longlong_t)r->clr_seq, (u_longlong_t)r->clr_txg, tbuf,
	    opname(r->clr_op), (u_longlong_t)r->clr_object,
	    (u_longlong_t)r->clr_parent);
	if (r->clr_flags & CL_F_RENAME2) {
		(void) printf("%s -> %s",
		    r->clr_namelen > 0 ? r->clr_name : "",
		    r->clr_name + r->clr_namelen + 1);
	} else {
		(void) printf("%s%s", r->clr_namelen > 0 ? r->clr_name : "-",
		    (r->clr_flags & CL_F_NAME_TRUNC) ? "*" : "");
	}

	if (paths) {
		(void) printf("  %s",
		    obj_to_path(dsname, r->clr_object, pbuf, sizeof (pbuf)));
	}
	(void) printf("\n");
}

static int
cmd_register(int argc, char **argv)
{
	nvlist_t *in, *out = NULL;
	uint64_t mask = 0;
	int err;

	if (argc < 4) {
		(void) fprintf(stderr,
		    "usage: zchangelog register <dataset> <consumer> "
		    "[op,op,...]\n");
		return (2);
	}
	if (argc > 4) {
		char *dup = strdup(argv[4]), *tok, *save = NULL;

		for (tok = strtok_r(dup, ",", &save); tok != NULL;
		    tok = strtok_r(NULL, ",", &save)) {
			int op = op_by_name(tok);

			if (op < 0) {
				(void) fprintf(stderr, "unknown op: %s\n", tok);
				free(dup);
				return (2);
			}
			mask |= CL_OP_MASK(op);
		}
		free(dup);
	}

	in = fnvlist_alloc();
	fnvlist_add_string(in, ZFS_CHANGELOG_CONSUMER, argv[3]);
	fnvlist_add_uint64(in, ZFS_CHANGELOG_MASK, mask);
	err = cl_ioctl(ZFS_IOC_CHANGELOG_REGISTER, argv[2], in, &out);
	fnvlist_free(in);

	if (err != 0) {
		(void) fprintf(stderr, "register failed: %s\n", strerror(err));
		return (1);
	}
	(void) printf("registered '%s' on %s at seq %llu\n", argv[3], argv[2],
	    (u_longlong_t)fnvlist_lookup_uint64(out, ZFS_CHANGELOG_SEQ));
	fnvlist_free(out);
	return (0);
}

static int
cmd_simple(int argc, char **argv, unsigned long ioc, const char *what,
    boolean_t need_seq)
{
	nvlist_t *in;
	int err;

	if (argc < (need_seq ? 5 : 4)) {
		(void) fprintf(stderr, "usage: zchangelog %s <dataset> "
		    "<consumer>%s\n", what, need_seq ? " <seq>" : "");
		return (2);
	}
	in = fnvlist_alloc();
	fnvlist_add_string(in, ZFS_CHANGELOG_CONSUMER, argv[3]);
	if (need_seq)
		fnvlist_add_uint64(in, ZFS_CHANGELOG_SEQ, strtoull(argv[4],
		    NULL, 10));
	err = cl_ioctl(ioc, argv[2], in, NULL);
	fnvlist_free(in);

	if (err != 0) {
		(void) fprintf(stderr, "%s failed: %s\n", what, strerror(err));
		return (1);
	}
	return (0);
}

static int
do_read(const char *dsname, const char *consumer, uint64_t cursor,
    boolean_t use_cursor, uint64_t maxrecs, uint64_t wait_ms,
    boolean_t paths, boolean_t ack, uint64_t *cursorp, boolean_t quiet)
{
	nvlist_t *in, *out = NULL;
	uint8_t *recs = NULL;
	uint_t nbytes = 0;
	uint64_t nrecs, next, head, tail, gen;
	boolean_t gap = B_FALSE;
	int err;

	in = fnvlist_alloc();
	if (consumer != NULL)
		fnvlist_add_string(in, ZFS_CHANGELOG_CONSUMER, consumer);
	if (use_cursor)
		fnvlist_add_uint64(in, ZFS_CHANGELOG_CURSOR, cursor);
	fnvlist_add_uint64(in, ZFS_CHANGELOG_MAXRECS, maxrecs);
	fnvlist_add_uint64(in, ZFS_CHANGELOG_WAIT_MS, wait_ms);
	err = cl_ioctl(ZFS_IOC_CHANGELOG_READ, dsname, in, &out);
	fnvlist_free(in);
	if (err != 0) {
		(void) fprintf(stderr, "read failed: %s\n", strerror(err));
		return (1);
	}

	nrecs = fnvlist_lookup_uint64(out, ZFS_CHANGELOG_NRECS);
	next = fnvlist_lookup_uint64(out, ZFS_CHANGELOG_CURSOR);
	head = fnvlist_lookup_uint64(out, ZFS_CHANGELOG_HEAD);
	tail = fnvlist_lookup_uint64(out, ZFS_CHANGELOG_TAIL);
	gen = fnvlist_lookup_uint64(out, ZFS_CHANGELOG_GEN);

	gap = fnvlist_lookup_boolean_value(out, ZFS_CHANGELOG_GAP);
	if (gap) {
		(void) fprintf(stderr,
		    "WARNING: gap, records were dropped before they were "
		    "read (generation %llu); resync with a full scan\n",
		    (u_longlong_t)gen);
	}
	if (quiet && !jflag) {
		(void) printf("head %llu  tail %llu  generation %llu  "
		    "pending %llu\n", (u_longlong_t)head, (u_longlong_t)tail,
		    (u_longlong_t)gen, (u_longlong_t)(head - tail));
	} else if (!quiet && nrecs > 0 &&
	    nvlist_lookup_uint8_array(out, ZFS_CHANGELOG_RECORDS, &recs,
	    &nbytes) == 0) {
		for (uint64_t i = 0; i < nrecs; i++) {
			if (jflag) {
				jrec((changelog_rec_t *)(recs +
				    i * CL_REC_SIZE));
			} else {
				print_rec(dsname,
				    (changelog_rec_t *)(recs +
				    i * CL_REC_SIZE), paths);
			}
		}
	}
	if (jflag) {
		(void) printf("{\"type\":\"meta\",\"cursor\":%llu,"
		    "\"head\":%llu,\"tail\":%llu,\"generation\":%llu,"
		    "\"gap\":%s}\n",
		    (u_longlong_t)next, (u_longlong_t)head,
		    (u_longlong_t)tail, (u_longlong_t)gen,
		    gap ? "true" : "false");
	}

	if (ack && consumer != NULL && next > 0) {
		nvlist_t *cin = fnvlist_alloc();

		fnvlist_add_string(cin, ZFS_CHANGELOG_CONSUMER, consumer);
		fnvlist_add_uint64(cin, ZFS_CHANGELOG_SEQ, next);
		err = cl_ioctl(ZFS_IOC_CHANGELOG_CLEAR, dsname, cin, NULL);
		fnvlist_free(cin);
		if (err != 0) {
			(void) fprintf(stderr, "clear failed: %s\n",
			    strerror(err));
			fnvlist_free(out);
			return (1);
		}
	}

	if (cursorp != NULL)
		*cursorp = next;
	fnvlist_free(out);
	return (0);
}

static int
cmd_read(int argc, char **argv, boolean_t status)
{
	const char *consumer = NULL, *dsname;
	uint64_t cursor = 0, maxrecs = 1024, wait_ms = 0;
	boolean_t use_cursor = B_FALSE, follow = B_FALSE;
	boolean_t paths = B_FALSE, ack = B_FALSE;
	int c;

	if (argc < 3) {
		(void) fprintf(stderr, "usage: zchangelog read <dataset> "
		    "[-c consumer] [-s seq] [-n count] [-w ms] [-f] [-p] "
		    "[-a]\n");
		return (2);
	}
	dsname = argv[2];
	optind = 3;
	while ((c = getopt(argc, argv, "c:s:n:w:fpaj")) != -1) {
		switch (c) {
		case 'c':
			consumer = optarg;
			break;
		case 's':
			cursor = strtoull(optarg, NULL, 10);
			use_cursor = B_TRUE;
			break;
		case 'n':
			maxrecs = strtoull(optarg, NULL, 10);
			break;
		case 'w':
			wait_ms = strtoull(optarg, NULL, 10);
			break;
		case 'f':
			follow = B_TRUE;
			break;
		case 'p':
			paths = B_TRUE;
			break;
		case 'a':
			ack = B_TRUE;
			break;
		case 'j':
			jflag = B_TRUE;
			break;
		default:
			return (2);
		}
	}

	if (status) {
		/*
		 * Ask from past the end so the kernel only reports state:
		 * a cursor below the tail would be a real gap, not news.
		 */
		if (jflag) {
			/*
			 * status -j uses a real read with an empty result so
			 * the meta line carries head/tail/generation.
			 */
			return (do_read(dsname, NULL, UINT64_MAX, B_TRUE, 1,
			    0, B_FALSE, B_FALSE, NULL, B_FALSE));
		}
		return (do_read(dsname, NULL, UINT64_MAX, B_TRUE, 1, 0,
		    B_FALSE, B_FALSE, NULL, B_TRUE));
	}

	if (consumer == NULL && !use_cursor) {
		/* No consumer and no cursor: start from the oldest record. */
		cursor = 0;
		use_cursor = B_TRUE;
	}

	if (!follow)
		return (do_read(dsname, consumer, cursor, use_cursor, maxrecs,
		    wait_ms, paths, ack, NULL, B_FALSE));

	if (wait_ms == 0)
		wait_ms = 1000;
	for (;;) {
		uint64_t next = cursor;
		int err = do_read(dsname, consumer, cursor, use_cursor,
		    maxrecs, wait_ms, paths, ack, &next, B_FALSE);

		if (err != 0)
			return (err);
		cursor = next;
		use_cursor = B_TRUE;
		(void) fflush(stdout);
	}
}

int
main(int argc, char **argv)
{
	int ret;

	if (argc < 2) {
		(void) fprintf(stderr,
		    "usage: zchangelog <register|read|clear|unregister|status> "
		    "<dataset> ...\n");
		return (2);
	}

	g_fd = open(ZFS_DEV, O_RDWR | O_CLOEXEC);
	if (g_fd < 0) {
		(void) fprintf(stderr, "cannot open %s: %s\n", ZFS_DEV,
		    strerror(errno));
		return (1);
	}

	if (strcmp(argv[1], "register") == 0)
		ret = cmd_register(argc, argv);
	else if (strcmp(argv[1], "read") == 0)
		ret = cmd_read(argc, argv, B_FALSE);
	else if (strcmp(argv[1], "status") == 0)
		ret = cmd_read(argc, argv, B_TRUE);
	else if (strcmp(argv[1], "clear") == 0)
		ret = cmd_simple(argc, argv, ZFS_IOC_CHANGELOG_CLEAR, "clear",
		    B_TRUE);
	else if (strcmp(argv[1], "unregister") == 0)
		ret = cmd_simple(argc, argv, ZFS_IOC_CHANGELOG_UNREGISTER,
		    "unregister", B_FALSE);
	else {
		(void) fprintf(stderr, "unknown subcommand: %s\n", argv[1]);
		ret = 2;
	}

	(void) close(g_fd);
	return (ret);
}
