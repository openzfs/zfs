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
 * Copyright (c) 2019 by Delphix. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/avl.h>
#include <sys/btree.h>
#include <sys/time.h>
#include <sys/resource.h>

#define	BUFSIZE 256

int seed = 0;
int stress_timeout = 180;
int contents_frequency = 100;
int tree_limit = 64 * 1024;
boolean_t stress_only = B_FALSE;

static void
usage(int exit_value)
{
	(void) fprintf(stderr, "Usage:\tbtree_test -n <test_name>\n");
	(void) fprintf(stderr, "\tbtree_test -s [-r <seed>] [-l <limit>] "
	    "[-t timeout>] [-c check_contents]\n");
	(void) fprintf(stderr, "\tbtree_test [-r <seed>] [-l <limit>] "
	    "[-t timeout>] [-c check_contents]\n");
	(void) fprintf(stderr, "\n    With the -n option, run the named "
	    "negative test. With the -s option,\n");
	(void) fprintf(stderr, "    run the stress test according to the "
	    "other options passed. With\n");
	(void) fprintf(stderr, "    neither, run all the positive tests, "
	    "including the stress test with\n");
	(void) fprintf(stderr, "    the default options.\n");
	(void) fprintf(stderr, "\n    Options that control the stress test\n");
	(void) fprintf(stderr, "\t-c stress iterations after which to compare "
	    "tree contents [default: 100]\n");
	(void) fprintf(stderr, "\t-l the largest value to allow in the tree "
	    "[default: 1M]\n");
	(void) fprintf(stderr, "\t-r random seed [default: from "
	    "gettimeofday()]\n");
	(void) fprintf(stderr, "\t-t seconds to let the stress test run "
	    "[default: 180]\n");
	exit(exit_value);
}

typedef struct int_node {
	avl_node_t node;
	uint64_t data;
} int_node_t;

/*
 * Utility functions
 */

static int
avl_compare(const void *v1, const void *v2)
{
	const int_node_t *n1 = v1;
	const int_node_t *n2 = v2;
	uint64_t a = n1->data;
	uint64_t b = n2->data;

	return (TREE_CMP(a, b));
}

static int
zfs_btree_compare(const void *v1, const void *v2)
{
	const uint64_t *a = v1;
	const uint64_t *b = v2;

	return (TREE_CMP(*a, *b));
}

static void
verify_contents(avl_tree_t *avl, zfs_btree_t *bt)
{
	static int count = 0;
	zfs_btree_index_t bt_idx = {0};
	int_node_t *node;
	uint64_t *data;

	boolean_t forward = count % 2 == 0 ? B_TRUE : B_FALSE;
	count++;

	ASSERT3U(avl_numnodes(avl), ==, zfs_btree_numnodes(bt));
	if (forward == B_TRUE) {
		node = avl_first(avl);
		data = zfs_btree_first(bt, &bt_idx);
	} else {
		node = avl_last(avl);
		data = zfs_btree_last(bt, &bt_idx);
	}

	while (node != NULL) {
		ASSERT3U(*data, ==, node->data);
		if (forward == B_TRUE) {
			data = zfs_btree_next(bt, &bt_idx, &bt_idx);
			node = AVL_NEXT(avl, node);
		} else {
			data = zfs_btree_prev(bt, &bt_idx, &bt_idx);
			node = AVL_PREV(avl, node);
		}
	}
}

static void
verify_node(avl_tree_t *avl, zfs_btree_t *bt, int_node_t *node)
{
	zfs_btree_index_t bt_idx = {0};
	zfs_btree_index_t bt_idx2 = {0};
	int_node_t *inp;
	uint64_t data = node->data;
	uint64_t *rv = NULL;

	ASSERT3U(avl_numnodes(avl), ==, zfs_btree_numnodes(bt));
	ASSERT3P((rv = (uint64_t *)zfs_btree_find(bt, &data, &bt_idx)), !=,
	    NULL);
	ASSERT3S(*rv, ==, data);
	ASSERT3P(zfs_btree_get(bt, &bt_idx), !=, NULL);
	ASSERT3S(data, ==, *(uint64_t *)zfs_btree_get(bt, &bt_idx));

	if ((inp = AVL_NEXT(avl, node)) != NULL) {
		ASSERT3P((rv = zfs_btree_next(bt, &bt_idx, &bt_idx2)), !=,
		    NULL);
		ASSERT3P(rv, ==, zfs_btree_get(bt, &bt_idx2));
		ASSERT3S(inp->data, ==, *rv);
	} else {
		ASSERT3U(data, ==, *(uint64_t *)zfs_btree_last(bt, &bt_idx));
	}

	if ((inp = AVL_PREV(avl, node)) != NULL) {
		ASSERT3P((rv = zfs_btree_prev(bt, &bt_idx, &bt_idx2)), !=,
		    NULL);
		ASSERT3P(rv, ==, zfs_btree_get(bt, &bt_idx2));
		ASSERT3S(inp->data, ==, *rv);
	} else {
		ASSERT3U(data, ==, *(uint64_t *)zfs_btree_first(bt, &bt_idx));
	}
}

/*
 * Tests
 */

/* Verify that zfs_btree_find works correctly with a NULL index. */
static int
find_without_index(zfs_btree_t *bt, char *why)
{
	u_longlong_t *p, i = 12345;

	zfs_btree_add(bt, &i);
	if ((p = (u_longlong_t *)zfs_btree_find(bt, &i, NULL)) == NULL ||
	    *p != i) {
		snprintf(why, BUFSIZE, "Unexpectedly found %llu\n",
		    p == NULL ? 0 : *p);
		return (1);
	}

	i++;

	if ((p = (u_longlong_t *)zfs_btree_find(bt, &i, NULL)) != NULL) {
		snprintf(why, BUFSIZE, "Found bad value: %llu\n", *p);
		return (1);
	}

	return (0);
}

/* Verify simple insertion and removal from the tree. */
static int
insert_find_remove(zfs_btree_t *bt, char *why)
{
	u_longlong_t *p, i = 12345;
	zfs_btree_index_t bt_idx = {0};

	/* Insert 'i' into the tree, and attempt to find it again. */
	zfs_btree_add(bt, &i);
	if ((p = (u_longlong_t *)zfs_btree_find(bt, &i, &bt_idx)) == NULL) {
		snprintf(why, BUFSIZE, "Didn't find value in tree\n");
		return (1);
	} else if (*p != i) {
		snprintf(why, BUFSIZE, "Found (%llu) in tree\n", *p);
		return (1);
	}
	ASSERT3S(zfs_btree_numnodes(bt), ==, 1);
	zfs_btree_verify(bt);

	/* Remove 'i' from the tree, and verify it is not found. */
	zfs_btree_remove(bt, &i);
	if ((p = (u_longlong_t *)zfs_btree_find(bt, &i, &bt_idx)) != NULL) {
		snprintf(why, BUFSIZE, "Found removed value (%llu)\n", *p);
		return (1);
	}
	ASSERT3S(zfs_btree_numnodes(bt), ==, 0);
	zfs_btree_verify(bt);

	return (0);
}

/*
 * Add a number of random entries into a btree and avl tree. Then walk them
 * backwards and forwards while emptying the tree, verifying the trees look
 * the same.
 */
static int
drain_tree(zfs_btree_t *bt, char *why)
{
	uint64_t *p;
	avl_tree_t avl;
	int i = 0;
	int_node_t *node;
	avl_index_t avl_idx = {0};
	zfs_btree_index_t bt_idx = {0};

	avl_create(&avl, avl_compare, sizeof (int_node_t),
	    offsetof(int_node_t, node));

	/* Fill both trees with the same data */
	for (i = 0; i < 64 * 1024; i++) {
		void *ret;

		u_longlong_t randval = random();
		node = malloc(sizeof (int_node_t));
		if ((p = (uint64_t *)zfs_btree_find(bt, &randval, &bt_idx)) !=
		    NULL) {
			continue;
		}
		zfs_btree_add_idx(bt, &randval, &bt_idx);

		node->data = randval;
		if ((ret = avl_find(&avl, node, &avl_idx)) != NULL) {
			snprintf(why, BUFSIZE, "Found in avl: %llu\n", randval);
			return (1);
		}
		avl_insert(&avl, node, avl_idx);
	}

	/* Remove data from either side of the trees, comparing the data */
	while (avl_numnodes(&avl) != 0) {
		uint64_t *data;

		ASSERT3U(avl_numnodes(&avl), ==, zfs_btree_numnodes(bt));
		if (avl_numnodes(&avl) % 2 == 0) {
			node = avl_first(&avl);
			data = zfs_btree_first(bt, &bt_idx);
		} else {
			node = avl_last(&avl);
			data = zfs_btree_last(bt, &bt_idx);
		}
		ASSERT3U(node->data, ==, *data);
		zfs_btree_remove_idx(bt, &bt_idx);
		avl_remove(&avl, node);

		if (avl_numnodes(&avl) == 0) {
			break;
		}

		node = avl_first(&avl);
		ASSERT3U(node->data, ==,
		    *(uint64_t *)zfs_btree_first(bt, NULL));
		node = avl_last(&avl);
		ASSERT3U(node->data, ==, *(uint64_t *)zfs_btree_last(bt, NULL));
	}
	ASSERT3S(zfs_btree_numnodes(bt), ==, 0);

	void *avl_cookie = NULL;
	while ((node = avl_destroy_nodes(&avl, &avl_cookie)) != NULL)
		free(node);
	avl_destroy(&avl);

	return (0);
}

/*
 * This test uses an avl and btree, and continually processes new random
 * values. Each value is either removed or inserted, depending on whether
 * or not it is found in the tree. The test periodically checks that both
 * trees have the same data and does consistency checks. This stress
 * option can also be run on its own from the command line.
 */
static int
stress_tree(zfs_btree_t *bt, char *why)
{
	avl_tree_t avl;
	int_node_t *node;
	struct timeval tp;
	time_t t0;
	int insertions = 0, removals = 0, iterations = 0;
	u_longlong_t max = 0, min = UINT64_MAX;

	(void) gettimeofday(&tp, NULL);
	t0 = tp.tv_sec;

	avl_create(&avl, avl_compare, sizeof (int_node_t),
	    offsetof(int_node_t, node));

	while (1) {
		zfs_btree_index_t bt_idx = {0};
		avl_index_t avl_idx = {0};

		uint64_t randval = random() % tree_limit;
		node = malloc(sizeof (*node));
		node->data = randval;

		max = randval > max ? randval : max;
		min = randval < min ? randval : min;

		void *ret = avl_find(&avl, node, &avl_idx);
		if (ret == NULL) {
			insertions++;
			avl_insert(&avl, node, avl_idx);
			ASSERT3P(zfs_btree_find(bt, &randval, &bt_idx), ==,
			    NULL);
			zfs_btree_add_idx(bt, &randval, &bt_idx);
			verify_node(&avl, bt, node);
		} else {
			removals++;
			verify_node(&avl, bt, ret);
			zfs_btree_remove(bt, &randval);
			avl_remove(&avl, ret);
			free(ret);
			free(node);
		}

		zfs_btree_verify(bt);

		iterations++;
		if (iterations % contents_frequency == 0) {
			verify_contents(&avl, bt);
		}

		zfs_btree_verify(bt);

		(void) gettimeofday(&tp, NULL);
		if (tp.tv_sec > t0 + stress_timeout) {
			fprintf(stderr, "insertions/removals: %u/%u\nmax/min: "
			    "%llu/%llu\n", insertions, removals, max, min);
			break;
		}
	}

	void *avl_cookie = NULL;
	while ((node = avl_destroy_nodes(&avl, &avl_cookie)) != NULL)
		free(node);
	avl_destroy(&avl);

	if (stress_only) {
		zfs_btree_index_t *idx = NULL;
		uint64_t *rv;

		while ((rv = zfs_btree_destroy_nodes(bt, &idx)) != NULL)
			;
		zfs_btree_verify(bt);
	}

	return (0);
}

/*
 * Verify inserting a duplicate value will cause a crash.
 * Note: negative test; return of 0 is a failure.
 */
static int
insert_duplicate(zfs_btree_t *bt)
{
	uint64_t *p, i = 23456;
	zfs_btree_index_t bt_idx = {0};

	if ((p = (uint64_t *)zfs_btree_find(bt, &i, &bt_idx)) != NULL) {
		fprintf(stderr, "Found value in empty tree.\n");
		return (0);
	}
	zfs_btree_add_idx(bt, &i, &bt_idx);
	if ((p = (uint64_t *)zfs_btree_find(bt, &i, &bt_idx)) == NULL) {
		fprintf(stderr, "Did not find expected value.\n");
		return (0);
	}

	/* Crash on inserting a duplicate */
	zfs_btree_add_idx(bt, &i, NULL);

	return (0);
}

/*
 * Verify removing a non-existent value will cause a crash.
 * Note: negative test; return of 0 is a failure.
 */
static int
remove_missing(zfs_btree_t *bt)
{
	uint64_t *p, i = 23456;
	zfs_btree_index_t bt_idx = {0};

	if ((p = (uint64_t *)zfs_btree_find(bt, &i, &bt_idx)) != NULL) {
		fprintf(stderr, "Found value in empty tree.\n");
		return (0);
	}

	/* Crash removing a nonexistent entry */
	zfs_btree_remove(bt, &i);

	return (0);
}

static int
do_negative_test(zfs_btree_t *bt, char *test_name)
{
	int rval = 0;
	struct rlimit rlim = {0};
	setrlimit(RLIMIT_CORE, &rlim);

	if (strcmp(test_name, "insert_duplicate") == 0) {
		rval = insert_duplicate(bt);
	} else if (strcmp(test_name, "remove_missing") == 0) {
		rval = remove_missing(bt);
	}

	/*
	 * Return 0, since callers will expect non-zero return values for
	 * these tests, and we should have crashed before getting here anyway.
	 */
	(void) fprintf(stderr, "Test: %s returned %d.\n", test_name, rval);
	return (0);
}

typedef struct btree_test {
	const char	*name;
	int		(*func)(zfs_btree_t *, char *);
} btree_test_t;

static btree_test_t test_table[] = {
	{ "insert_find_remove",		insert_find_remove	},
	{ "find_without_index",		find_without_index	},
	{ "drain_tree",			drain_tree		},
	{ "stress_tree",		stress_tree		},
	{ NULL,				NULL			}
};

int
main(int argc, char *argv[])
{
	char *negative_test = NULL;
	int failed_tests = 0;
	struct timeval tp;
	zfs_btree_t bt;
	char c;

	while ((c = getopt(argc, argv, "c:l:n:r:st:")) != -1) {
		switch (c) {
		case 'c':
			contents_frequency = atoi(optarg);
			break;
		case 'l':
			tree_limit = atoi(optarg);
			break;
		case 'n':
			negative_test = optarg;
			break;
		case 'r':
			seed = atoi(optarg);
			break;
		case 's':
			stress_only = B_TRUE;
			break;
		case 't':
			stress_timeout = atoi(optarg);
			break;
		case 'h':
		default:
			usage(1);
			break;
		}
	}
	argc -= optind;
	argv += optind;
	optind = 1;


	if (seed == 0) {
		(void) gettimeofday(&tp, NULL);
		seed = tp.tv_sec;
	}
	srandom(seed);

	zfs_btree_init();
	zfs_btree_create(&bt, zfs_btree_compare, sizeof (uint64_t));

	/*
	 * This runs the named negative test. None of them should
	 * return, as they both cause crashes.
	 */
	if (negative_test) {
		return (do_negative_test(&bt, negative_test));
	}

	fprintf(stderr, "Seed: %u\n", seed);

	/*
	 * This is a stress test that does operations on a btree over the
	 * requested timeout period, verifying them against identical
	 * operations in an avl tree.
	 */
	if (stress_only != 0) {
		return (stress_tree(&bt, NULL));
	}

	/* Do the positive tests */
	btree_test_t *test = &test_table[0];
	while (test->name) {
		int retval;
		uint64_t *rv;
		char why[BUFSIZE] = {0};
		zfs_btree_index_t *idx = NULL;

		(void) fprintf(stdout, "%-20s", test->name);
		retval = test->func(&bt, why);

		if (retval == 0) {
			(void) fprintf(stdout, "ok\n");
		} else {
			(void) fprintf(stdout, "failed with %d\n", retval);
			if (strlen(why) != 0)
				(void) fprintf(stdout, "\t%s\n", why);
			why[0] = '\0';
			failed_tests++;
		}

		/* Remove all the elements and re-verify the tree */
		while ((rv = zfs_btree_destroy_nodes(&bt, &idx)) != NULL)
			;
		zfs_btree_verify(&bt);

		test++;
	}

	zfs_btree_verify(&bt);
	zfs_btree_fini();

	return (failed_tests);
}
