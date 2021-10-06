/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2021 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>

/*
 * Possible keys in nvlist requests / responses to/from the Agent
 */
#define	AGENT_TYPE			"Type"
#define	AGENT_TYPE_CREATE_POOL		"create pool"
#define	AGENT_TYPE_CREATE_POOL_DONE	"pool create done"
#define	AGENT_TYPE_OPEN_POOL		"open pool"
#define	AGENT_TYPE_OPEN_POOL_DONE	"pool open done"
#define	AGENT_TYPE_OPEN_POOL_FAILED	"pool open failed"
#define	AGENT_TYPE_READ_BLOCK		"read block"
#define	AGENT_TYPE_READ_DONE		"read done"
#define	AGENT_TYPE_WRITE_BLOCK		"write block"
#define	AGENT_TYPE_WRITE_DONE		"write done"
#define	AGENT_TYPE_FREE_BLOCK		"free block"
#define	AGENT_TYPE_BEGIN_TXG		"begin txg"
#define	AGENT_TYPE_RESUME_TXG		"resume txg"
#define	AGENT_TYPE_RESUME_COMPLETE	"resume complete"
#define	AGENT_TYPE_END_TXG		"end txg"
#define	AGENT_TYPE_END_TXG_DONE		"end txg done"
#define	AGENT_TYPE_FLUSH_WRITES		"flush writes"
#define	AGENT_TYPE_EXIT			"exit agent"
#define	AGENT_TYPE_CLOSE_POOL		"close pool"
#define	AGENT_TYPE_CLOSE_POOL_DONE	"pool close done"
#define	AGENT_TYPE_ENABLE_FEATURE	"enable feature"
#define	AGENT_TYPE_ENABLE_FEATURE_DONE	"enable feature done"
#define	AGENT_TYPE_GET_POOLS		"get pools"
#define	AGENT_TYPE_GET_DESTROYING_POOLS	"get destroying pools"
#define	AGENT_TYPE_GET_DESTROYING_POOLS_DONE "get destroying pools done"
#define	AGENT_TYPE_CLEAR_DESTROYED_POOLS "clear destroyed pools"
#define	AGENT_TYPE_RESUME_DESTROY_POOL	"resume destroy pool"
#define	AGENT_TYPE_RESUME_DESTROY_POOL_DONE "resume destroy pool done"

#define	AGENT_NAME			"name"
#define	AGENT_SIZE			"size"
#define	AGENT_TXG			"TXG"
#define	AGENT_GUID			"GUID"
#define	AGENT_BUCKET			"bucket"
#define	AGENT_CRED_PROFILE		"credentials_profile"
#define	AGENT_ENDPOINT			"endpoint"
#define	AGENT_REGION			"region"
#define	AGENT_BLKID			"block"
#define	AGENT_DATA			"data"
#define	AGENT_REQUEST_ID		"request_id"
#define	AGENT_UBERBLOCK			"uberblock"
#define	AGENT_CONFIG			"config"
#define	AGENT_NEXT_BLOCK		"next_block"
#define	AGENT_TOKEN			"token"
#define	AGENT_CAUSE			"cause"
#define	AGENT_HOSTNAME			"hostname"
#define	AGENT_READONLY			"readonly"
#define	AGENT_RESUME			"resume"
#define	AGENT_HEAL			"heal"
#define	AGENT_FEATURE			"feature"
#define	AGENT_FEATURES			"features"
#define	AGENT_REFCOUNT			"refcount"
#define	AGENT_CAN_READONLY		"can_readonly"
#define	AGENT_DESTROY			"destroy"
#define	AGENT_DESTROY_DOMPLETED		"destroy_completed"
#define	AGENT_START_TIME		"start_time"
#define	AGENT_TOTAL_DATA_OBJECTS	"total_data_objects"
#define	AGENT_DESTROYED_OBJECTS		"destroyed_objects"
#define	AGENT_POOLS			"pools"
#define	AGENT_MESSAGE			"message"

typedef struct vdev_object_store_stats {
	uint64_t voss_blocks_count;
	uint64_t voss_blocks_bytes;
	uint64_t voss_pending_frees_count;
	uint64_t voss_pending_frees_bytes;
	uint64_t voss_objects_count;
} vdev_object_store_stats_t;

void object_store_begin_txg(vdev_t *, uint64_t);
void object_store_end_txg(vdev_t *, nvlist_t *, uint64_t);
void object_store_free_block(vdev_t *, uint64_t, uint64_t);
void object_store_flush_writes(spa_t *, uint64_t);
void object_store_restart_agent(vdev_t *vd);
void object_store_get_stats(vdev_t *, vdev_object_store_stats_t *);
