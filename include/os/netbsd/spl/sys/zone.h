// SPDX-License-Identifier: BSD-2-Clause
#ifndef _OPENSOLARIS_SYS_ZONE_H_
#define	_OPENSOLARIS_SYS_ZONE_H_

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/cred.h>

#define	GLOBAL_ZONEID	0
#define	INGLOBALZONE(proc)	(1)

extern int zone_dataset_attach(cred_t *, const char *, int);
extern int zone_dataset_detach(cred_t *, const char *, int);
extern int zone_dataset_visible(const char *, int *);
extern uint32_t zone_get_hostid(void *);

typedef enum zone_uid_op {
	ZONE_OP_CREATE,
	ZONE_OP_SNAPSHOT,
	ZONE_OP_CLONE,
	ZONE_OP_DESTROY,
	ZONE_OP_RENAME,
	ZONE_OP_SETPROP
} zone_uid_op_t;

typedef enum zone_admin_result {
	ZONE_ADMIN_NOT_APPLICABLE,
	ZONE_ADMIN_ALLOWED,
	ZONE_ADMIN_DENIED
} zone_admin_result_t;

static inline zone_admin_result_t
zone_dataset_admin_check(const char *dataset, zone_uid_op_t op,
    const char *aux_dataset)
{
	(void) dataset, (void) op, (void) aux_dataset;
	return (ZONE_ADMIN_NOT_APPLICABLE);
}

typedef uid_t (*zone_get_zoned_uid_fn_t)(const char *dataset,
    char *root_out, size_t root_size);

static inline int
zone_dataset_attach_uid(cred_t *cred, const char *dataset, uid_t uid)
{
	(void) cred, (void) dataset, (void) uid;
	return (ENXIO);
}

static inline int
zone_dataset_detach_uid(cred_t *cred, const char *dataset, uid_t uid)
{
	(void) cred, (void) dataset, (void) uid;
	return (ENXIO);
}

static inline void
zone_register_zoned_uid_callback(zone_get_zoned_uid_fn_t fn)
{
	(void) fn;
}

static inline void
zone_unregister_zoned_uid_callback(void)
{
}

#endif	/* !_OPENSOLARIS_SYS_ZONE_H_ */
