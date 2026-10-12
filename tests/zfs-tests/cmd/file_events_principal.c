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

/*
 * Test helper for per-dataset file event principal tags.
 *
 * The `zfs events -j` output intentionally drops the uid and principal
 * fields, so tests must read raw records via lzc_get_events() to assert
 * on them.  This helper provides the three operations the tests need:
 *
 *   write <path>   register principal 0xdeadbeef, write the file from
 *                  the same process, print the registration generation
 *   read <dataset> dump records: "REC txg= op= name= uid= principal="
 *   clear          deregister the caller, print the generation
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <libzfs_core.h>
#include <sys/nvpair.h>

#define	FEP_PRINCIPAL	0xdeadbeefULL

static void
usage(const char *name)
{
	(void) fprintf(stderr,
	    "usage: %s write <path>\n"
	    "       %s read <dataset>\n"
	    "       %s clear\n", name, name, name);
	exit(2);
}

static uint64_t
record_u64(nvlist_t *nvl, const char *key, boolean_t *found)
{
	uint64_t val = 0;
	uint16_t val16 = 0;

	*found = (nvlist_lookup_uint64(nvl, key, &val) == 0);
	if (!*found)
		*found = (nvlist_lookup_uint16(nvl, key, &val16) == 0);
	if (!*found)
		return (0);
	return (*found ? (val16 ? (uint64_t)val16 : val) : 0);
}

static void
print_record(nvlist_t *nvl)
{
	const char *op = NULL;
	const char *name = NULL;
	uint64_t txg = 0;
	boolean_t has_uid = B_FALSE;
	boolean_t has_principal = B_FALSE;
	uint64_t uid = 0;
	uint64_t principal = 0;

	(void) nvlist_lookup_string(nvl, "op", &op);
	(void) nvlist_lookup_string(nvl, "name", &name);
	txg = record_u64(nvl, "txg", &has_uid);
	uid = record_u64(nvl, "uid", &has_uid);
	principal = record_u64(nvl, "principal", &has_principal);

	(void) printf("REC txg=%llu op=%s name=%s uid=", (u_longlong_t)txg,
	    op ? op : "?", name ? name : "?");
	if (has_uid)
		(void) printf("%llu", (u_longlong_t)uid);
	else
		(void) printf("none");
	if (has_principal)
		(void) printf(" principal=%llu\n", (u_longlong_t)principal);
	else
		(void) printf(" principal=none\n");
}

static int
do_write(const char *path)
{
	uint64_t gen = 0;
	FILE *fp;
	const char *data = "principal-probe\n";
	int error;

	error = lzc_set_principal(FEP_PRINCIPAL, &gen);
	if (error != 0) {
		(void) fprintf(stderr, "lzc_set_principal failed: %d\n",
		    error);
		return (1);
	}
	(void) printf("%llu\n", (u_longlong_t)gen);

	fp = fopen(path, "w");
	if (fp == NULL) {
		(void) fprintf(stderr, "fopen(%s) failed\n", path);
		return (1);
	}
	if (fwrite(data, 1, strlen(data), fp) != strlen(data) ||
	    fclose(fp) != 0) {
		(void) fprintf(stderr, "write to %s failed\n", path);
		return (1);
	}

	/*
	 * Registrations are keyed by thread group, so deregister here:
	 * a later `clear` invocation would run as a different tgid and
	 * find no entry (ENOENT).
	 */
	if (lzc_clear_principal(&gen) != 0) {
		(void) fprintf(stderr, "lzc_clear_principal failed\n");
		return (1);
	}
	return (0);
}

static int
do_read(const char *dsname)
{
	nvlist_t *outnvl = NULL;
	nvlist_t *events_list = NULL;
	nvlist_t *rec = NULL;
	nvpair_t *pair = NULL;
	int error;

	error = lzc_get_events(dsname, 0, 0, &outnvl);
	if (error != 0) {
		(void) fprintf(stderr, "lzc_get_events(%s) failed: %d\n",
		    dsname, error);
		return (1);
	}
	if (outnvl == NULL)
		return (0);
	/*
	 * The kernel returns "events" as a flat nvlist keyed by
	 * record index string ("0", "1", ...), not an nvlist array
	 * (zfs_ioctl.c zfs_ioc_get_events).
	 */
	if (nvlist_lookup_nvlist(outnvl, "events", &events_list) != 0) {
		fnvlist_free(outnvl);
		return (0);
	}
	pair = nvlist_next_nvpair(events_list, NULL);
	for (; pair != NULL; pair = nvlist_next_nvpair(events_list, pair)) {
		if (nvpair_value_nvlist(pair, &rec) == 0)
			print_record(rec);
	}
	fnvlist_free(outnvl);
	return (0);
}

static int
do_clear(void)
{
	uint64_t gen = 0;
	int error;

	error = lzc_clear_principal(&gen);
	if (error != 0) {
		(void) fprintf(stderr, "lzc_clear_principal failed: %d\n",
		    error);
		return (1);
	}
	(void) printf("%llu\n", (u_longlong_t)gen);
	return (0);
}

int
main(int argc, char **argv)
{
	(void) libzfs_core_init();

	if (argc < 2)
		usage(argv[0]);
	if (strcmp(argv[1], "write") == 0 && argc == 3)
		return (do_write(argv[2]));
	if (strcmp(argv[1], "read") == 0 && argc == 3)
		return (do_read(argv[2]));
	if (strcmp(argv[1], "clear") == 0 && argc == 2)
		return (do_clear());
	usage(argv[0]);
	return (2);
}
