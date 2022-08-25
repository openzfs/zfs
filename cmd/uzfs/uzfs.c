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
 * Copyright (c) 2022, SmartX Inc. All rights reserved.
 */

#include <libintl.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <libzfs.h>
#include <locale.h>
#include <getopt.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/zfs_context.h>
#include <libuzfs.h>

static int uzfs_zpool_create(int argc, char **argv);
static int uzfs_zpool_destroy(int argc, char **argv);
static int uzfs_zpool_set(int argc, char **argv);
static int uzfs_zpool_get(int argc, char **argv);

static int uzfs_dataset_create(int argc, char **argv);
static int uzfs_dataset_destroy(int argc, char **argv);

static int uzfs_object_create(int argc, char **argv);
static int uzfs_object_delete(int argc, char **argv);
static int uzfs_object_claim(int argc, char **argv);
static int uzfs_object_stat(int argc, char **argv);
static int uzfs_object_list(int argc, char **argv);
static int uzfs_object_read(int argc, char **argv);
static int uzfs_object_write(int argc, char **argv);

typedef enum {
	HELP_ZPOOL_CREATE,
	HELP_ZPOOL_DESTROY,
	HELP_ZPOOL_SET,
	HELP_ZPOOL_GET,
	HELP_DATASET_CREATE,
	HELP_DATASET_DESTROY,
	HELP_OBJECT_CREATE,
	HELP_OBJECT_DELETE,
	HELP_OBJECT_CLAIM,
	HELP_OBJECT_STAT,
	HELP_OBJECT_LIST,
	HELP_OBJECT_READ,
	HELP_OBJECT_WRITE,
} uzfs_help_t;

typedef struct uzfs_command {
	const char	*name;
	int		(*func)(int argc, char **argv);
	uzfs_help_t	usage;
} uzfs_command_t;

/*
 * Master command table.  Each ZFS command has a name, associated function, and
 * usage message.  The usage messages need to be internationalized, so we have
 * to have a function to return the usage message based on a command index.
 *
 * These commands are organized according to how they are displayed in the usage
 * message.  An empty command (one with a NULL name) indicates an empty line in
 * the generic usage message.
 */
static uzfs_command_t command_table[] = {
	{ "create-zpool",	uzfs_zpool_create, 	HELP_ZPOOL_CREATE   },
	{ "destroy-zpool",	uzfs_zpool_destroy, 	HELP_ZPOOL_DESTROY  },
	{ "set-zpool",		uzfs_zpool_set, 	HELP_ZPOOL_SET    },
	{ "get-zpool",		uzfs_zpool_get, 	HELP_ZPOOL_GET    },
	{ "create-dataset",	uzfs_dataset_create, 	HELP_DATASET_CREATE },
	{ "destroy-dataset",	uzfs_dataset_destroy, 	HELP_DATASET_DESTROY},
	{ "create-object",	uzfs_object_create, 	HELP_OBJECT_CREATE  },
	{ "delete-object",	uzfs_object_delete, 	HELP_OBJECT_DELETE  },
	{ "claim-object",	uzfs_object_claim, 	HELP_OBJECT_CLAIM   },
	{ "stat-object",	uzfs_object_stat, 	HELP_OBJECT_STAT    },
	{ "list-object",	uzfs_object_list, 	HELP_OBJECT_LIST    },
	{ "read-object",	uzfs_object_read, 	HELP_OBJECT_READ    },
	{ "write-object",	uzfs_object_write, 	HELP_OBJECT_WRITE   },
};

#define	NCOMMAND	(sizeof (command_table) / sizeof (command_table[0]))

uzfs_command_t *current_command;

static const char *
get_usage(uzfs_help_t idx)
{
	switch (idx) {
	case HELP_ZPOOL_CREATE:
		return (gettext("\tcreate-zpool ...\n"));
	case HELP_ZPOOL_DESTROY:
		return (gettext("\tdestroy-zpool ...\n"));
	case HELP_ZPOOL_SET:
		return (gettext("\tset-zpool ...\n"));
	case HELP_ZPOOL_GET:
		return (gettext("\tget-zpool ...\n"));
	case HELP_DATASET_CREATE:
		return (gettext("\tcreate-dataset ...\n"));
	case HELP_DATASET_DESTROY:
		return (gettext("\tdestroy-dataset ...\n"));
	case HELP_OBJECT_CREATE:
		return (gettext("\tcreate-object ...\n"));
	case HELP_OBJECT_DELETE:
		return (gettext("\tdelete-object ...\n"));
	case HELP_OBJECT_STAT:
		return (gettext("\tstat-object ...\n"));
	case HELP_OBJECT_LIST:
		return (gettext("\tlist-object ...\n"));
	case HELP_OBJECT_READ:
		return (gettext("\tread-object ...\n"));
	case HELP_OBJECT_WRITE:
		return (gettext("\twrite-object ...\n"));
	case HELP_OBJECT_CLAIM:
		return (gettext("\tclaim-object ...\n"));
	default:
		__builtin_unreachable();
	}
}

/*
 * Display usage message.  If we're inside a command, display only the usage for
 * that command.  Otherwise, iterate over the entire command table and display
 * a complete usage message.
 */
static void
usage(boolean_t requested)
{
	int i;
	FILE *fp = requested ? stdout : stderr;

	if (current_command == NULL) {
		(void) fprintf(fp, gettext("usage: uzfs command args ...\n"));
		(void) fprintf(fp,
		    gettext("where 'command' is one of the following:\n\n"));

		for (i = 0; i < NCOMMAND; i++) {
			if (command_table[i].name == NULL)
				(void) fprintf(fp, "\n");
			else
				(void) fprintf(fp, "%s",
				    get_usage(command_table[i].usage));
		}

		(void) fprintf(fp, gettext("\nEach dataset is of the form: "
		    "pool/[dataset/]*dataset[@name]\n"));
	} else {
		(void) fprintf(fp, gettext("usage:\n"));
		(void) fprintf(fp, "%s", get_usage(current_command->usage));
	}

	/*
	 * See comments at end of main().
	 */
	if (getenv("ZFS_ABORT") != NULL) {
		(void) printf("dumping core by request\n");
		abort();
	}

	exit(requested ? 0 : 2);
}

static int
find_command_idx(char *command, int *idx)
{
	int i;

	for (i = 0; i < NCOMMAND; i++) {
		if (command_table[i].name == NULL)
			continue;

		if (strcmp(command, command_table[i].name) == 0) {
			*idx = i;
			return (0);
		}
	}
	return (-1);
}

int
main(int argc, char **argv)
{
	int error = 0;
	int i = 0;
	char *cmdname;
	char **newargv;

	(void) setlocale(LC_ALL, "");
	(void) setlocale(LC_NUMERIC, "C");
	(void) textdomain(TEXT_DOMAIN);

	opterr = 0;

	/*
	 * Make sure the user has specified some command.
	 */
	if (argc < 2) {
		(void) fprintf(stderr, gettext("missing command\n"));
		usage(B_FALSE);
	}

	dprintf_setup(&argc, argv);

	cmdname = argv[1];

	libuzfs_set_zpool_cache_path("/tmp/zpool.cache");

	libuzfs_init();

	/*
	 * Many commands modify input strings for string parsing reasons.
	 * We create a copy to protect the original argv.
	 */
	newargv = malloc((argc + 1) * sizeof (newargv[0]));
	for (i = 0; i < argc; i++)
		newargv[i] = strdup(argv[i]);
	newargv[argc] = NULL;

	/*
	 * Run the appropriate command.
	 */
	if (find_command_idx(cmdname, &i) == 0) {
		current_command = &command_table[i];
		error = command_table[i].func(argc - 1, newargv + 1);
	} else if (strchr(cmdname, '=') != NULL) {
		verify(find_command_idx("set", &i) == 0);
		current_command = &command_table[i];
		error = command_table[i].func(argc, newargv);
	} else {
		(void) fprintf(stderr, gettext("unrecognized "
		    "command '%s'\n"), cmdname);
		usage(B_FALSE);
		error = 1;
	}

	for (i = 0; i < argc; i++)
		free(newargv[i]);
	free(newargv);

	libuzfs_fini();

	/*
	 * The 'ZFS_ABORT' environment variable causes us to dump core on exit
	 * for the purposes of running ::findleaks.
	 */
	if (getenv("ZFS_ABORT") != NULL) {
		(void) printf("dumping core by request\n");
		abort();
	}

	return (error);
}

int
uzfs_zpool_create(int argc, char **argv)
{
	int err = 0;
	char *zpool = argv[1];
	char *path = argv[2];

	printf("creating zpool %s, devpath: %s\n", zpool, path);

	err = libuzfs_zpool_create(zpool, path, NULL, NULL);
	if (err)
		printf("failed to create zpool: %s, path: %s\n", zpool, path);

	return (err);
}

int
uzfs_zpool_destroy(int argc, char **argv)
{
	int err = 0;
	char *zpool = argv[1];

	printf("destroying zpool %s\n", zpool);

	err = libuzfs_zpool_destroy(zpool);
	if (err)
		printf("failed to destroy zpool: %s\n", zpool);

	return (err);
}

int
uzfs_zpool_set(int argc, char **argv)
{
	char *zpool = argv[1];
	char *prop_name = argv[2];
	uint64_t value = atoi(argv[3]);

	printf("setting zpool %s, %s=%lu\n", zpool, prop_name, value);

	libuzfs_zpool_handle_t *zhp;
	zhp = libuzfs_zpool_open(zpool);
	if (!zhp) {
		printf("failed to open zpool: %s\n", zpool);
		return (-1);
	}

	zpool_prop_t prop = zpool_name_to_prop(prop_name);
	libuzfs_zpool_prop_set(zhp, prop, value);

	libuzfs_zpool_close(zhp);

	return (0);
}

int
uzfs_zpool_get(int argc, char **argv)
{
	int err = 0;
	char *zpool = argv[1];
	char *prop_name = argv[2];

	printf("getting zpool %s, %s\n", zpool, prop_name);

	libuzfs_zpool_handle_t *zhp;
	zhp = libuzfs_zpool_open(zpool);
	if (!zhp) {
		printf("failed to open zpool: %s\n", zpool);
		return (-1);
	}

	uint64_t value = 0;
	zpool_prop_t prop = zpool_name_to_prop(prop_name);
	err = libuzfs_zpool_prop_get(zhp, prop, &value);
	if (err)
		printf("failed to get pool: %s, prop: %s\n", zpool, prop_name);
	else
		printf("prop: %s=%ld\n", prop_name, value);

	libuzfs_zpool_close(zhp);

	return (err);
}

int
uzfs_dataset_create(int argc, char **argv)
{
	int err = 0;
	char *dsname = argv[1];

	printf("creating dataset %s\n", dsname);

	err = libuzfs_dataset_create(dsname);
	if (err)
		printf("failed to create dataset: %s\n", dsname);

	return (err);
}

int
uzfs_dataset_destroy(int argc, char **argv)
{
	char *dsname = argv[1];

	printf("destroying dataset %s\n", dsname);

	libuzfs_dataset_destroy(dsname);

	return (0);
}

int
uzfs_object_create(int argc, char **argv)
{
	int err = 0;
	char *dsname = argv[1];

	printf("creating object %s\n", dsname);

	libuzfs_dataset_handle_t *dhp = libuzfs_dataset_open(dsname);
	if (!dhp) {
		printf("failed to open dataset: %s\n", dsname);
		return (-1);
	}

	uint64_t obj = 0;

	err = libuzfs_object_create(dhp, &obj);
	if (err)
		printf("failed to create object on dataset: %s\n", dsname);
	else
		printf("created object %s:%ld\n", dsname, obj);

	libuzfs_dataset_close(dhp);

	return (err);
}

int
uzfs_object_delete(int argc, char **argv)
{
	int err = 0;
	char *dsname = argv[1];
	uint64_t obj = atoi(argv[2]);

	printf("destroying object %s:%ld\n", dsname, obj);

	libuzfs_dataset_handle_t *dhp = libuzfs_dataset_open(dsname);
	if (!dhp) {
		printf("failed to open dataset: %s\n", dsname);
		return (-1);
	}

	err = libuzfs_object_delete(dhp, obj);
	if (err)
		printf("failed to delete object: %s:%ld\n", dsname, obj);

	libuzfs_dataset_close(dhp);
	return (0);
}

int
uzfs_object_claim(int argc, char **argv)
{
	int err = 0;
	char *dsname = argv[1];
	uint64_t obj = atoi(argv[2]);

	printf("claiming object %s:%ld\n", dsname, obj);

	libuzfs_dataset_handle_t *dhp = libuzfs_dataset_open(dsname);
	if (!dhp) {
		printf("failed to open dataset: %s\n", dsname);
		return (-1);
	}

	err = libuzfs_object_claim(dhp, obj);
	if (err)
		printf("failed to claim object on dataset: %s\n", dsname);

	libuzfs_dataset_close(dhp);

	return (err);
}

static char *
uzfs_ot_name(dmu_object_type_t type)
{
	if (type < DMU_OT_NUMTYPES)
		return (dmu_ot[type].ot_name);
	else if ((type & DMU_OT_NEWTYPE) &&
	    ((type & DMU_OT_BYTESWAP_MASK) < DMU_BSWAP_NUMFUNCS))
		return (dmu_ot_byteswap[type & DMU_OT_BYTESWAP_MASK].ob_name);
	else
		return ("UNKNOWN");
}

static void
uzfs_dump_doi(uint64_t object, dmu_object_info_t *doi)
{
	printf("object: %ld\n", object);
	printf("\tdata_block_size: %d\n", doi->doi_data_block_size);
	printf("\tmetadata_block_size: %d\n", doi->doi_metadata_block_size);
	printf("\ttype: %s\n", uzfs_ot_name(doi->doi_type));
	printf("\tbonus_type: %s\n", uzfs_ot_name(doi->doi_bonus_type));
	printf("\tbonus_size: %ld\n", doi->doi_bonus_size);
	printf("\tindirection: %d\n", doi->doi_indirection);
	printf("\tchecksum: %d\n", doi->doi_checksum);
	printf("\tcompress: %d\n", doi->doi_compress);
	printf("\tnblkptr: %d\n", doi->doi_nblkptr);
	printf("\tdnodesize: %ld\n", doi->doi_dnodesize);
	printf("\tphysical_blocks_512: %ld\n", doi->doi_physical_blocks_512);
	printf("\tmax_offset: %ld\n", doi->doi_max_offset);
	printf("\tfill_count: %ld\n", doi->doi_fill_count);
}

int
uzfs_object_stat(int argc, char **argv)
{
	int err = 0;
	char *dsname = argv[1];
	uint64_t obj = atoi(argv[2]);

	printf("stating object %s:%ld\n", dsname, obj);

	libuzfs_dataset_handle_t *dhp = libuzfs_dataset_open(dsname);
	if (!dhp) {
		printf("failed to open dataset: %s\n", dsname);
		return (-1);
	}

	dmu_object_info_t doi;
	memset(&doi, 0, sizeof (doi));

	err = libuzfs_object_stat(dhp, obj, &doi);
	if (err)
		printf("failed to stat object: %s:%ld\n", dsname, obj);
	else
		uzfs_dump_doi(obj, &doi);


	libuzfs_dataset_close(dhp);
	return (0);
}

int
uzfs_object_list(int argc, char **argv)
{
	char *dsname = argv[1];

	printf("listing objects in %s\n", dsname);

	libuzfs_dataset_handle_t *dhp = libuzfs_dataset_open(dsname);
	if (!dhp) {
		printf("failed to open dataset: %s\n", dsname);
		return (-1);
	}

	libuzfs_object_list(dhp);

	libuzfs_dataset_close(dhp);
	return (0);
}

int
uzfs_object_read(int argc, char **argv)
{
	int err = 0;
	char *dsname = argv[1];
	uint64_t obj = atoi(argv[2]);
	int offset = atoi(argv[3]);
	int size = atoi(argv[4]);
	char *buf = NULL;

	printf("reading %s: %ld, off: %d, size: %d\n", dsname, obj, offset,
	    size);

	libuzfs_dataset_handle_t *dhp = libuzfs_dataset_open(dsname);
	if (!dhp) {
		printf("failed to open dataset: %s\n", dsname);
		return (-1);
	}

	buf = umem_zalloc(size + 1, UMEM_NOFAIL);

	err = libuzfs_object_read(dhp, obj, offset, size, buf);
	if (err)
		printf("failed to read object: %s:%ld\n", dsname, obj);
	else
		printf("read %s: %ld, off: %d, size: %d\n%s\n", dsname, obj,
		    offset, size, buf);

	umem_free(buf, size + 1);
	libuzfs_dataset_close(dhp);
	return (0);
}

int
uzfs_object_write(int argc, char **argv)
{
	int err = 0;
	char *dsname = argv[1];
	uint64_t obj = atoi(argv[2]);
	int offset = atoi(argv[3]);
	int size = strlen(argv[4]);
	char *buf = NULL;

	printf("writing %s: %ld, off: %d, size: %d\n", dsname, obj, offset,
	    size);

	libuzfs_dataset_handle_t *dhp = libuzfs_dataset_open(dsname);
	if (!dhp) {
		printf("failed to open dataset: %s\n", dsname);
		return (-1);
	}

	buf = umem_alloc(size, UMEM_NOFAIL);
	memcpy(buf, argv[4], size);

	err = libuzfs_object_write(dhp, obj, offset, size, buf);
	if (err)
		printf("failed to write object: %s:%ld\n", dsname, obj);

	umem_free(buf, size);
	libuzfs_dataset_close(dhp);
	return (0);
}
