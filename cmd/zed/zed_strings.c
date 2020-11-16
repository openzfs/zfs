/*
 * This file is part of the ZFS Event Daemon (ZED).
 *
 * Developed at Lawrence Livermore National Laboratory (LLNL-CODE-403049).
 * Copyright (C) 2013-2014 Lawrence Livermore National Security, LLC.
 * Refer to the ZoL git commit log for authoritative copyright attribution.
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License Version 1.0 (CDDL-1.0).
 * You can obtain a copy of the license from the top-level file
 * "OPENSOLARIS.LICENSE" or at <http://opensource.org/licenses/CDDL-1.0>.
 * You may not use this file except in compliance with the license.
 */

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/avl.h>
#include <sys/sysmacros.h>
#include "zed_strings.h"

struct zed_strings {
	avl_tree_t tree;
	avl_node_t *iteratorp;
};

struct zed_strings_node {
	avl_node_t node;
	char *key;
	char *val;
};

typedef struct zed_strings_node zed_strings_node_t;

/*
 * Compare zed_strings_node_t nodes [x1] and [x2].
 * As required for the AVL tree, return -1 for <, 0 for ==, and +1 for >.
 */
static int
_zed_strings_node_compare(const void *x1, const void *x2)
{
	const char *s1;
	const char *s2;
	int rv;

	assert(x1 != NULL);
	assert(x2 != NULL);

	s1 = ((const zed_strings_node_t *) x1)->key;
	assert(s1 != NULL);
	s2 = ((const zed_strings_node_t *) x2)->key;
	assert(s2 != NULL);
	rv = strcmp(s1, s2);

	if (rv < 0)
		return (-1);

	if (rv > 0)
		return (1);

	return (0);
}

/*
 * Return a new string container, or NULL on error.
 */
zed_strings_t *
zed_strings_create(void)
{
	zed_strings_t *zsp;

	zsp = calloc(1, sizeof (*zsp));
	if (!zsp)
		return (NULL);

	avl_create(&zsp->tree, _zed_strings_node_compare,
	    sizeof (zed_strings_node_t), offsetof(zed_strings_node_t, node));

	zsp->iteratorp = NULL;
	return (zsp);
}

/*
 * Destroy the string node [np].
 */
static void
_zed_strings_node_destroy(zed_strings_node_t *np)
{
	if (!np)
		return;

	if (np->key) {
		if (np->key != np->val)
			free(np->key);
		np->key = NULL;
	}
	if (np->val) {
		free(np->val);
		np->val = NULL;
	}
	free(np);
}

/*
 * Return a new string node for storing the string [val], or NULL on error.
 * If [key] is specified, it will be used to index the node; otherwise,
 * the string [val] will be used.
 */
static zed_strings_node_t *
_zed_strings_node_create(const char *key, const char *val)
{
	zed_strings_node_t *np;

	assert(val != NULL);

	np = calloc(1, sizeof (*np));
	if (!np)
		return (NULL);

	np->val = strdup(val);
	if (!np->val)
		goto nomem;

	if (key) {
		np->key = strdup(key);
		if (!np->key)
			goto nomem;
	} else {
		np->key = np->val;
	}
	return (np);

nomem:
	_zed_strings_node_destroy(np);
	return (NULL);
}

/*
 * Destroy the string container [zsp] and all nodes within.
 */
void
zed_strings_destroy(zed_strings_t *zsp)
{
	void *cookie;
	zed_strings_node_t *np;

	if (!zsp)
		return;

	cookie = NULL;
	while ((np = avl_destroy_nodes(&zsp->tree, &cookie)))
		_zed_strings_node_destroy(np);

	avl_destroy(&zsp->tree);
	free(zsp);
}

/*
 * Add a copy of the string [s] indexed by [key] to the container [zsp].
 * If [key] already exists within the container [zsp], it will be replaced
 * with the new string [s].
 * If [key] is NULL, the string [s] will be used as the key.
 * Return 0 on success, or -1 on error.
 */
int
zed_strings_add(zed_strings_t *zsp, const char *key, const char *s)
{
	zed_strings_node_t *newp, *oldp;

	if (!zsp || !s) {
		errno = EINVAL;
		return (-1);
	}
	if (key == s)
		key = NULL;

	newp = _zed_strings_node_create(key, s);
	if (!newp)
		return (-1);

	oldp = avl_find(&zsp->tree, newp, NULL);
	if (oldp) {
		avl_remove(&zsp->tree, oldp);
		_zed_strings_node_destroy(oldp);
	}
	avl_add(&zsp->tree, newp);
	return (0);
}

/*
 * Return the first string in container [zsp].
 * Return NULL if there are no strings, or on error.
 * This can be called multiple times to re-traverse [zsp].
 * XXX: Not thread-safe.
 */
const char *
zed_strings_first(zed_strings_t *zsp)
{
	if (!zsp) {
		errno = EINVAL;
		return (NULL);
	}
	zsp->iteratorp = avl_first(&zsp->tree);
	if (!zsp->iteratorp)
		return (NULL);

	return (((zed_strings_node_t *)zsp->iteratorp)->val);

}

/*
 * Return the next string in container [zsp].
 * Return NULL after the last string, or on error.
 * This must be called after zed_strings_first().
 * XXX: Not thread-safe.
 */
const char *
zed_strings_next(zed_strings_t *zsp)
{
	if (!zsp) {
		errno = EINVAL;
		return (NULL);
	}
	if (!zsp->iteratorp)
		return (NULL);

	zsp->iteratorp = AVL_NEXT(&zsp->tree, zsp->iteratorp);
	if (!zsp->iteratorp)
		return (NULL);

	return (((zed_strings_node_t *)zsp->iteratorp)->val);
}

/*
 * Return the number of strings in container [zsp], or -1 on error.
 */
int
zed_strings_count(zed_strings_t *zsp)
{
	if (!zsp) {
		errno = EINVAL;
		return (-1);
	}
	return (avl_numnodes(&zsp->tree));
}
