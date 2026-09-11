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
#ifndef _SPL_PROCFS_LIST_H
#define	_SPL_PROCFS_LIST_H

#ifdef  __cplusplus
extern "C" {
#endif

typedef struct procfs_list procfs_list_t;
struct procfs_list {
	void			*pl_private;
	void			*pl_next_data;
	kmutex_t		pl_lock;
	list_t			pl_list;
	uint64_t		pl_next_id;
	int			(*pl_show)(struct seq_file *f, void *p);
	int			(*pl_show_header)(struct seq_file *f);
	int			(*pl_clear)(procfs_list_t *procfs_list);
	size_t			pl_node_offset;
};

typedef struct procfs_list_node {
	list_node_t		pln_link;
	uint64_t		pln_id;
} procfs_list_node_t;

void procfs_list_install(const char *module,
    const char *submodule,
    const char *name,
    mode_t mode,
    procfs_list_t *procfs_list,
    int (*show)(struct seq_file *f, void *p),
    int (*show_header)(struct seq_file *f),
    int (*clear)(procfs_list_t *procfs_list),
    size_t procfs_list_node_off);
void procfs_list_uninstall(procfs_list_t *procfs_list);
void procfs_list_destroy(procfs_list_t *procfs_list);
void procfs_list_add(procfs_list_t *procfs_list, void *p);

#ifdef  __cplusplus
}
#endif

#endif
