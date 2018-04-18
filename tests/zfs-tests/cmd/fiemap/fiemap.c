/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright (c) 2018, Lawrence Livermore National Security, LLC.
 */

#include "../file_common.h"
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/space_reftree.h>
#include <sys/fiemap.h>
#include <linux/fs.h>
#include <linux/fiemap.h>

typedef enum verify_tree_type {
	VERIFY_DATA_TREE,
	VERIFY_HOLE_TREE,
} verify_tree_type_t;

typedef enum verify_mode {
	VERIFY_MODE_EQUAL,
	VERIFY_MODE_GT,
	VERIFY_MODE_LT,
	VERIFY_MODE_ALL,
} verify_mode_t;

typedef struct fiemap_args {
	struct fiemap	*fa_fiemap;
	char		*fa_filename;
	int		fa_fd;
	struct stat	fa_statbuf;
	unsigned	fa_flags;
	boolean_t	fa_verbose;
	boolean_t	fa_verify_data;
	boolean_t	fa_verify_size;
	boolean_t	fa_verify_hole;
	boolean_t	fa_verify_flags;
	boolean_t	fa_verify_dev;
	boolean_t	fa_verify_extent_count;
	avl_tree_t	fa_verify_trees[2];
	unsigned	fa_verify_sizes[2];
	unsigned	fa_verify_index;
	char		*fa_verify_flags_str;
	verify_mode_t	fa_verify_flags_mode;
	int		fa_verify_flags_count;
	unsigned	fa_verify_dev_id;
	verify_mode_t	fa_verify_dev_mode;
	int		fa_verify_dev_count;
	int		fa_verify_extent_expected;
} fiemap_args_t;

static int
usage(char *msg, int exit_value)
{
	(void) fprintf(stderr, "fiemap [-achsv?] "
	    "[[-DH] <offset:length:refs>] [-F <flags:[=<>count>]\n"
	    "    [-V <vdev:[=<>]count>] [-E extent-count] filename\n");

	if (msg != NULL)
		(void) fprintf(stderr, "%s\n", msg);

	return (exit_value);
}

static int
fiemap_ioctl(fiemap_args_t *fa)
{
	struct fiemap *fiemap;
	size_t size = sizeof (struct fiemap);
	unsigned extents;
	int error;


	fiemap = calloc(1, size);
	if (fiemap == NULL)
		return (errno);

	/*
	 * Request the number of extents
	 */
	fiemap->fm_start = 0;
	fiemap->fm_length = FIEMAP_MAX_OFFSET;
	fiemap->fm_flags = fa->fa_flags;
	fiemap->fm_extent_count = 0;
	fiemap->fm_mapped_extents = 0;

	error = ioctl(fa->fa_fd, FS_IOC_FIEMAP, fiemap);
	if (error < 0) {
		free(fiemap);
		return (errno);
	}

	extents = fiemap->fm_mapped_extents;
	size += sizeof (struct fiemap_extent) * extents;
	free(fiemap);

	fiemap = calloc(1, size);
	if (fiemap == NULL)
		return (errno);

	/*
	 * Read all reported extents.
	 */
	fiemap->fm_start = 0;
	fiemap->fm_length = FIEMAP_MAX_OFFSET;
	fiemap->fm_flags = fa->fa_flags;
	fiemap->fm_extent_count = extents;
	fiemap->fm_mapped_extents = 0;

	error = ioctl(fa->fa_fd, FS_IOC_FIEMAP, fiemap);
	if (error < 0) {
		free(fiemap);
		return (errno);
	}

	fa->fa_fiemap = fiemap;

	return (0);
}

static char *
fiemap_extent_flags_str(struct fiemap_extent *extent, char *str, int size)
{
	unsigned flags = extent->fe_flags;
	char *next = str;

	str[0] = '\0';

	if (flags & FIEMAP_EXTENT_LAST)
		next += snprintf(next, size - (next - str), "last,");
	if (flags & FIEMAP_EXTENT_UNKNOWN)
		next += snprintf(next, size - (next - str), "unknown,");
	if (flags & FIEMAP_EXTENT_DELALLOC)
		next += snprintf(next, size - (next - str), "delalloc,");
	if (flags & FIEMAP_EXTENT_ENCODED)
		next += snprintf(next, size - (next - str), "encoded,");
	if (flags & FIEMAP_EXTENT_DATA_ENCRYPTED)
		next += snprintf(next, size - (next - str), "data-encrypted,");
	if (flags & FIEMAP_EXTENT_NOT_ALIGNED)
		next += snprintf(next, size - (next - str), "not-aligned,");
	if (flags & FIEMAP_EXTENT_DATA_INLINE)
		next += snprintf(next, size - (next - str), "data-inline,");
	if (flags & FIEMAP_EXTENT_DATA_TAIL)
		next += snprintf(next, size - (next - str), "data-tail,");
	if (flags & FIEMAP_EXTENT_UNWRITTEN)
		next += snprintf(next, size - (next - str), "unwritten,");
	if (flags & FIEMAP_EXTENT_MERGED)
		next += snprintf(next, size - (next - str), "merged,");
	if (flags & FIEMAP_EXTENT_SHARED)
		next += snprintf(next, size - (next - str), "shared,");
	if (next > str)
		next[-1] = '\0';

	return (str);
}

#define	PRINT_LOGICAL		1
#define	PRINT_PHYSICAL		2

static char *
fiemap_extent_str(struct fiemap_extent *extent, int type, char *str, int size)
{
	uint64_t start = 0, end = 0, len = 0;
	unsigned flags = extent->fe_flags;

	switch (type) {
	case PRINT_LOGICAL:
		len = extent->fe_length;
		start = extent->fe_logical;
		if (start || len)
			end = extent->fe_logical + len - 1;
		break;
	case PRINT_PHYSICAL:
		if (!(flags & FIEMAP_EXTENT_UNWRITTEN)) {
			len = extent->fe_physical_length_reserved;
			start = extent->fe_physical;
			if (start || len)
				end = extent->fe_physical + len - 1;
		}
		break;
	}

	(void) snprintf(str, size, "0x%012llx-0x%012llx %9llu",
	    (u_longlong_t)start, (u_longlong_t)end, (u_longlong_t)len);

	return (str);
}

static void
fiemap_print(fiemap_args_t *fa)
{
	char lstr[64], pstr[64], fstr[128];

	printf("Extents: %u\n", fa->fa_fiemap->fm_mapped_extents);
	printf("%-4s %-39s %-39s %-3s %-s\n", "ID",
	    "Logical (Start-End Length)", "Physical (Start-End Length)",
	    "Dev", "Flags");

	for (int i = 0; i < fa->fa_fiemap->fm_mapped_extents; i++) {
		struct fiemap_extent *extent = &fa->fa_fiemap->fm_extents[i];

		printf("%-4d %s %s %-3u %s\n", i,
		    fiemap_extent_str(extent, PRINT_LOGICAL, lstr, 64),
		    fiemap_extent_str(extent, PRINT_PHYSICAL, pstr, 64),
		    extent->fe_device_reserved,
		    fiemap_extent_flags_str(extent, fstr, 128));
	}
}

static void
fiemap_verify_count_cb(void *arg, uint64_t offset, uint64_t size)
{
	fiemap_args_t *fa = (fiemap_args_t *)arg;
	fa->fa_verify_sizes[fa->fa_verify_index]++;
}

static void
fiemap_verify_print_cb(void *arg, uint64_t offset, uint64_t size)
{
	fiemap_args_t *fa = (fiemap_args_t *)arg;
	unsigned i = fa->fa_verify_index;

	if (fa->fa_verbose == B_TRUE) {
		printf("%-4d 0x%012llx-0x%012llx %9llu\n",
		    fa->fa_verify_sizes[i], (u_longlong_t)offset,
		    (u_longlong_t)offset + size, (u_longlong_t)size);
	}

	fa->fa_verify_sizes[i]++;
}

static void
fiemap_verify_extent_dec(void *arg, uint64_t start, uint64_t size)
{
	space_reftree_add_seg(arg, start, start + size, -1);
}

/*
 * When a logical extent mapping has been provided, using -V, verify it
 * against the list of returned extents.  This is accomplished by first
 * building up a reference tree of all the expected logical extents.
 * Then for each extent reported by FIEMAP decrease the reference counts.
 * After iterating over all the extents generate a range tree containing
 * all references >= -1.  The resulting tree must be empty for all extents
 * to be properly accounted for.
 */
static int
fiemap_verify_extents(fiemap_args_t *fa, verify_tree_type_t type)
{
	avl_tree_t *t = &fa->fa_verify_trees[type];
	struct fiemap_extent *ext;
	range_tree_t *rt;
	boolean_t is_data = (type == VERIFY_DATA_TREE);
	boolean_t is_hole = (type == VERIFY_HOLE_TREE);
	int error = 0;

	rt = range_tree_create(NULL, NULL);
	if (rt == NULL)
		return (ENOMEM);

	if (is_data) {
		/*
		 * All data extents will be reported, the provided space
		 * reference tree only needs to decrement the given ranges.
		 */
		for (int i = 0; i < fa->fa_fiemap->fm_mapped_extents; i++) {
			ext = &fa->fa_fiemap->fm_extents[i];

			if (!(ext->fe_flags & FIEMAP_EXTENT_UNWRITTEN))
				fiemap_verify_extent_dec(t, ext->fe_logical,
				    ext->fe_length);
		}
	} else if (is_hole && fa->fa_flags & FIEMAP_FLAG_HOLES) {
		/*
		 * FIEMAP_FLAG_HOLES was passes so all hole extents will be
		 * reported, the provided space reference tree only needs to
		 * decrement the given ranges.
		 */
		for (int i = 0; i < fa->fa_fiemap->fm_mapped_extents; i++) {
			ext = &fa->fa_fiemap->fm_extents[i];

			if (ext->fe_flags & FIEMAP_EXTENT_UNWRITTEN)
				fiemap_verify_extent_dec(t, ext->fe_logical,
				    ext->fe_length);
		}
	} else if (is_hole) {
		avl_tree_t ht;

		/*
		 * Holes will not be reported and must be calculated based
		 * on the lack of a data extent for the range.  This is
		 * accomplished by creating a space reference tree which
		 * contains a single range the length of the file.  Then the
		 * reported data ranges are removed.  What's left with a
		 * positive reference count are the holes.  These can then
		 * be decremented from provided hole tree for verification.
		 */
		space_reftree_create(&ht);
		space_reftree_add_seg(&ht, 0, fa->fa_statbuf.st_size, 1);

		for (int i = 0; i < fa->fa_fiemap->fm_mapped_extents; i++) {
			ext = &fa->fa_fiemap->fm_extents[i];

			if (!(ext->fe_flags & FIEMAP_EXTENT_UNWRITTEN))
				fiemap_verify_extent_dec(&ht, ext->fe_logical,
				    ext->fe_length);
		}

		space_reftree_generate_map(&ht, rt, 1);
		range_tree_walk(rt, fiemap_verify_extent_dec, t);
		range_tree_vacate(rt, NULL, NULL);

		space_reftree_destroy(&ht);
	}

	space_reftree_generate_map(t, rt, 1);

	fa->fa_verify_index = type;
	range_tree_walk(rt, fiemap_verify_count_cb, fa);

	if (fa->fa_verbose == B_TRUE && fa->fa_verify_sizes[type] > 0) {
		printf("----- Missing %s Tree Extents -----\n",
		    is_data ? "Data" : "Hole");
		printf("%-4s %-39s\n", "ID", "Logical (Start-End Length)");

		fa->fa_verify_sizes[type] = 0;
		range_tree_walk(rt, fiemap_verify_print_cb, fa);
	}

	range_tree_vacate(rt, NULL, NULL);
	range_tree_destroy(rt);

	/*
	 * There are additional logical extents reported by FIEMAP which
	 * were not included in the provided space reference tree.
	 */
	if (space_reftree_is_empty(t) == 0) {
		printf("%s verify failed, additional extents found\n",
		    is_data ? "Data" : "Hole");
		error = EDOM;
	}

	/*
	 * There are missing logical extents not reported by FIEMAP which
	 * were expected given the provided space reference tree.
	 */
	if (fa->fa_verify_sizes[type] > 0) {
		printf("%s verify failed, %d extent(s) missing\n",
		    is_data ? "Data" : "Hole", fa->fa_verify_sizes[type]);
		error = EDOM;
	}

	return (error);
}

/*
 * When a list of extent flags has been provided verify that a certain
 * number of extents have the specified flags set.  VERIFY_MODE_ALL can be
 * used to indicate that all extents must include the flags
 */
static int
fiemap_verify_flags(fiemap_args_t *fa)
{
	char fstr[128];
	int count = 0, error = 0;

	for (int i = 0; i < fa->fa_fiemap->fm_mapped_extents; i++) {
		struct fiemap_extent *extent = &fa->fa_fiemap->fm_extents[i];

		(void) fiemap_extent_flags_str(extent, fstr, 128);
		if (strstr(fstr, fa->fa_verify_flags_str) != NULL)
			count++;
	}

	switch (fa->fa_verify_flags_mode) {
	case VERIFY_MODE_EQUAL:
		if (count != fa->fa_verify_flags_count) {
			printf("Exactly %d extents with '%s' required, "
			    "%d found\n", fa->fa_verify_flags_count,
			    fa->fa_verify_flags_str, count);
			error = EDOM;
		}
		break;
	case VERIFY_MODE_GT:
		if (count <= fa->fa_verify_flags_count) {
			printf("Greater than %d extents with '%s' required, "
			    "%d found\n", fa->fa_verify_flags_count,
			    fa->fa_verify_flags_str, count);
			error = EDOM;
		}
		break;
	case VERIFY_MODE_LT:
		if (count >= fa->fa_verify_flags_count) {
			printf("Fewer than %d extents with '%s' required, "
			    "%d found\n", fa->fa_verify_flags_count,
			    fa->fa_verify_flags_str, count);
			error = EDOM;
		}
		break;
	case VERIFY_MODE_ALL:
		if (count != fa->fa_fiemap->fm_mapped_extents) {
			printf("All %d extents with '%s' required, %d found\n",
			    fa->fa_fiemap->fm_mapped_extents,
			    fa->fa_verify_flags_str, count);
			error = EDOM;
		} else if (count == 0) {
			printf("No extents with flag '%s' were found\n",
			    fa->fa_verify_flags_str);
			error = EDOM;
		}
		break;
	default:
		error = EINVAL;
	}

	return (error);
}

/*
 * When a list of extent device ids has been provided verify that a certain
 * number of extents have the specified device id.  VERIFY_MODE_ALL can be
 * used to indicate that all extents must be for the device id.
 */
static int
fiemap_verify_device(fiemap_args_t *fa)
{
	int count = 0, error = 0;

	for (int i = 0; i < fa->fa_fiemap->fm_mapped_extents; i++) {
		struct fiemap_extent *extent = &fa->fa_fiemap->fm_extents[i];

		if (extent->fe_device_reserved == fa->fa_verify_dev_id)
			count++;
	}

	switch (fa->fa_verify_dev_mode) {
	case VERIFY_MODE_EQUAL:
		if (count != fa->fa_verify_dev_count) {
			printf("Exactly %d extents for device '%u' required, "
			    "%d found\n", fa->fa_verify_dev_count,
			    fa->fa_verify_dev_id, count);
			error = EDOM;
		}
		break;
	case VERIFY_MODE_GT:
		if (count <= fa->fa_verify_dev_count) {
			printf("Greater than %d extents for device '%u' "
			    "required, %d found\n", fa->fa_verify_dev_count,
			    fa->fa_verify_dev_id, count);
			error = EDOM;
		}
		break;
	case VERIFY_MODE_LT:
		if (count >= fa->fa_verify_dev_count) {
			printf("Fewer than %d extents for device '%u' "
			    "required, %d found\n", fa->fa_verify_dev_count,
			    fa->fa_verify_dev_id, count);
			error = EDOM;
		}
		break;
	case VERIFY_MODE_ALL:
		if (count != fa->fa_fiemap->fm_mapped_extents) {
			printf("All %d extents for device '%u' required, "
			    "%d found\n", fa->fa_fiemap->fm_mapped_extents,
			    fa->fa_verify_dev_id, count);
			error = EDOM;
		} else if (count == 0) {
			printf("No extents for device '%u' were found\n",
			    fa->fa_verify_dev_id);
			error = EDOM;
		}
		break;
	default:
		error = EINVAL;
	}

	return (error);
}

/*
 * Verify the reported extents cover the entire requested range.  This will
 * only be the case for sparse files when FIEMAP_FLAG_HOLES has been set and
 * holes are reported.
 */
static int
fiemap_verify_size(fiemap_args_t *fa)
{
	range_tree_t *rt = range_tree_create(NULL, NULL);
	int error = 0;

	for (int i = 0; i < fa->fa_fiemap->fm_mapped_extents; i++) {
		struct fiemap_extent *extent = &fa->fa_fiemap->fm_extents[i];
		range_tree_add(rt, extent->fe_logical, extent->fe_length);
	}

	if (range_tree_space(rt) != fa->fa_statbuf.st_size) {
		printf("The reported extents cover %llu / %llu bytes of "
		    "the file\n", (u_longlong_t)range_tree_space(rt),
		    (u_longlong_t)fa->fa_statbuf.st_size);
		error = EDOM;
	}

	range_tree_vacate(rt, NULL, NULL);
	range_tree_destroy(rt);

	return (error);
}

/*
 * Verify the expected number of extents are reported.
 */
static int
fiemap_verify_extent_count(fiemap_args_t *fa)
{
	int error = 0;

	if (fa->fa_verify_extent_expected != fa->fa_fiemap->fm_extent_count) {
		printf("Expected %d extents but %d reported\n",
		    fa->fa_verify_extent_expected,
		    fa->fa_fiemap->fm_extent_count);
		error = EDOM;
	}

	return (error);
}

/*
 * Verify reported extents cover the entire requested range.  Optionally,
 * perform additional checks on the reported extents based on the provided
 * command line options.  The file stats as reported by fstat(2) are cached
 * and may be used by the verification checks.
 */
static int
fiemap_verify(fiemap_args_t *fa)
{
	int error;

	error = fstat(fa->fa_fd, &fa->fa_statbuf);
	if (error)
		return (0x01);

	if (fa->fa_verify_size == B_TRUE && fiemap_verify_size(fa))
		error |= 0x02;

	if (fa->fa_verify_data == B_TRUE &&
	    fiemap_verify_extents(fa, VERIFY_DATA_TREE))
		error |= 0x04;

	if (fa->fa_verify_hole == B_TRUE &&
	    fiemap_verify_extents(fa, VERIFY_HOLE_TREE))
		error |= 0x08;

	if (fa->fa_verify_flags == B_TRUE && fiemap_verify_flags(fa))
		error |= 0x10;

	if (fa->fa_verify_dev == B_TRUE && fiemap_verify_device(fa))
		error |= 0x20;

	if (fa->fa_verify_extent_count == B_TRUE &&
	    fiemap_verify_extent_count(fa))
		error |= 0x40;

	return (error);
}

static void
fiemap_init(fiemap_args_t *fa)
{
	memset(fa, 0, sizeof (fiemap_args_t));

	range_tree_init();
	space_reftree_create(&fa->fa_verify_trees[VERIFY_DATA_TREE]);
	space_reftree_create(&fa->fa_verify_trees[VERIFY_HOLE_TREE]);
}

static void
fiemap_fini(fiemap_args_t *fa)
{
	(void) close(fa->fa_fd);
	free(fa->fa_verify_flags_str);

	space_reftree_destroy(&fa->fa_verify_trees[VERIFY_DATA_TREE]);
	space_reftree_destroy(&fa->fa_verify_trees[VERIFY_HOLE_TREE]);
	range_tree_fini();
}

static int
fiemap_open(fiemap_args_t *fa, char *filename)
{
	if (filename == NULL)
		return (EINVAL);

	if ((fa->fa_fd = open(filename, O_LARGEFILE | O_RDONLY)) < 0)
		return (errno);

	fa->fa_filename = filename;

	return (0);
}

int
main(int argc, char *argv[])
{
	fiemap_args_t fa;
	char *filename, *flags, *s;
	unsigned long long offset, length;
	long long refs;
	unsigned dev;
	int c, error, matched;

	fiemap_init(&fa);

	while ((c = getopt(argc, argv, "achsvD:E:H:F:V:?")) != -1) {
		switch (c) {
		case 'a':
			fa.fa_flags |= FIEMAP_FLAG_NOMERGE;
			break;
		case 'c':
			fa.fa_flags |= FIEMAP_FLAG_COPIES;
			break;
		case 'h':
			fa.fa_verify_size = B_TRUE;
			fa.fa_flags |= FIEMAP_FLAG_HOLES;
			break;
		case 's':
			fa.fa_flags |= FIEMAP_FLAG_SYNC;
			break;
		case 'v':
			fa.fa_verbose = B_TRUE;
			break;
		case 'D':
			matched = sscanf(optarg, "%llu:%llu:%lld",
			    &offset, &length, &refs);
			if (matched != 3) {
				error = usage("Use -D <offset:length:refs>", 1);
				goto out;
			}

			fa.fa_verify_data = B_TRUE;
			space_reftree_add_seg(
			    &fa.fa_verify_trees[VERIFY_DATA_TREE],
			    offset, offset + length, refs);
			break;
		case 'E':
			fa.fa_verify_extent_count = B_TRUE;
			fa.fa_verify_extent_expected = strtol(optarg, NULL, 0);
			break;
		case 'H':
			matched = sscanf(optarg, "%llu:%llu:%lld",
			    &offset, &length, &refs);
			if (matched != 3) {
				error = usage("Use -H <offset:length:refs>", 1);
				goto out;
			}

			fa.fa_verify_hole = B_TRUE;
			space_reftree_add_seg(
			    &fa.fa_verify_trees[VERIFY_HOLE_TREE],
			    offset, offset + length, refs);
			break;
		case 'V':
			matched = sscanf(optarg, "%u:%m[0-9a-z<>=-]", &dev, &s);
			if (matched != 2) {
				error = usage(
				    "Use -V <device:[<>=]count|all>", 1);
				goto out;
			}

			if (strncmp(s, "all", 3) == 0) {
				fa.fa_verify_dev_mode = VERIFY_MODE_ALL;
				fa.fa_verify_dev_count = 0;
			} else if (s[0] == '=') {
				fa.fa_verify_dev_mode = VERIFY_MODE_EQUAL;
				fa.fa_verify_dev_count = strtol(s+1, NULL, 0);
			} else if (s[0] == '<') {
				fa.fa_verify_dev_mode = VERIFY_MODE_LT;
				fa.fa_verify_dev_count = strtol(s+1, NULL, 0);
			} else if (s[0] == '>') {
				fa.fa_verify_dev_mode = VERIFY_MODE_GT;
				fa.fa_verify_dev_count = strtol(s+1, NULL, 0);
			} else {
				fa.fa_verify_dev_mode = VERIFY_MODE_EQUAL;
				fa.fa_verify_dev_count = strtol(s, NULL, 0);
			}

			fa.fa_verify_dev = B_TRUE;
			fa.fa_verify_dev_id = dev;
			free(s);
			break;
		case 'F':
			if (fa.fa_verify_flags == B_TRUE) {
				error = usage("-F passed more then once", 1);
				goto out;
			}

			matched = sscanf(optarg, "%m[a-z,-]:%m[0-9a-z<>=-]",
			    &flags, &s);
			if (matched != 2) {
				error = usage(
				    "Use -F <flags:[<>=]count|all>", 1);
				goto out;
			}

			if (strncmp(s, "all", 3) == 0) {
				fa.fa_verify_flags_mode = VERIFY_MODE_ALL;
				fa.fa_verify_flags_count = 0;
			} else if (s[0] == '=') {
				fa.fa_verify_flags_mode = VERIFY_MODE_EQUAL;
				fa.fa_verify_flags_count = strtol(s+1, NULL, 0);
			} else if (s[0] == '<') {
				fa.fa_verify_flags_mode = VERIFY_MODE_LT;
				fa.fa_verify_flags_count = strtol(s+1, NULL, 0);
			} else if (s[0] == '>') {
				fa.fa_verify_flags_mode = VERIFY_MODE_GT;
				fa.fa_verify_flags_count = strtol(s+1, NULL, 0);
			} else {
				fa.fa_verify_flags_mode = VERIFY_MODE_EQUAL;
				fa.fa_verify_flags_count = strtol(s, NULL, 0);
			}

			fa.fa_verify_flags = B_TRUE;
			fa.fa_verify_flags_str = flags;
			free(s);
			break;
		case '?':
			error = usage(NULL, 0);
			goto out;
		default:
			error = usage("Unknown option", 1);
			goto out;
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1) {
		error = usage("Incorrect number of arguments.", 1);
		goto out;
	}

	error = fiemap_open(&fa, argv[0]);
	if (error) {
		printf("Cannot open: %s (%d)\n",
		    filename ? filename : "<missing>", errno);
		error = 1;
		goto out;
	}

	error = fiemap_ioctl(&fa);
	if (error == 0) {
		error = fiemap_verify(&fa);

		if (fa.fa_verbose == B_TRUE)
			fiemap_print(&fa);
	} else {
		printf("Failed to read FIEMAP: %d\n", error);
		error = 1;
	}
out:
	fiemap_fini(&fa);

	return (error);
}
