/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license from the top-level
 * OPENSOLARIS.LICENSE or <http://opensource.org/licenses/CDDL-1.0>.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each file
 * and include the License file from the top-level OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Developed at Lawrence Livermore National Laboratory (LLNL-CODE-403049).
 * Copyright (C) 2013-2014 Lawrence Livermore National Security, LLC.
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
	char string[];
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

	s1 = ((const zed_strings_node_t *) x1)->string;
	assert(s1 != NULL);
	s2 = ((const zed_strings_node_t *) x2)->string;
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

	zsp = malloc(sizeof (*zsp));
	if (!zsp)
		return (NULL);

	memset(zsp, 0, sizeof (*zsp));
	avl_create(&zsp->tree, _zed_strings_node_compare,
	    sizeof (zed_strings_node_t), offsetof(zed_strings_node_t, node));

	zsp->iteratorp = NULL;
	return (zsp);
}

/*
 * Destroy the string container [zsp] and all strings within.
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
		free(np);

	avl_destroy(&zsp->tree);
	free(zsp);
}

/*
 * Add a copy of the string [s] to the container [zsp].
 * Return 0 on success, or -1 on error.
 *
 * FIXME: Handle dup strings.
 */
int
zed_strings_add(zed_strings_t *zsp, const char *s)
{
	size_t len;
	zed_strings_node_t *np;

	if (!zsp || !s) {
		errno = EINVAL;
		return (-1);
	}
	len = sizeof (zed_strings_node_t) + strlen(s) + 1;
	np = malloc(len);
	if (!np)
		return (-1);

	memset(np, 0, len);
	assert((char *) np->string + strlen(s) < (char *) np + len);
	(void) strcpy(np->string, s);
	avl_add(&zsp->tree, np);
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

	return (((zed_strings_node_t *) zsp->iteratorp)->string);

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

	return (((zed_strings_node_t *)zsp->iteratorp)->string);
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
