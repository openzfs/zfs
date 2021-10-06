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
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_trim.h>
#include <sys/vdev_object_store.h>
#include <sys/zio.h>
#include <sys/fs/zfs.h>
#include <sys/fm/fs/zfs.h>
#include <sys/abd.h>
#include <sys/metaslab_impl.h>
#include <sys/sock.h>
#include <sys/zap.h>

/*
 * Virtual device vector for object storage.
 */

/*
 * By default, the logical/physical ashift for object store vdevs is set to
 * SPA_MINBLOCKSHIFT (9). This allows all object store vdevs to use
 * 512B (1 << 9) blocksizes. Users may opt to change one or both of these
 * for testing or performance reasons. Care should be taken as these
 * values will impact the vdev_ashift setting which can only be set at
 * vdev creation time.
 */
unsigned long vdev_object_store_logical_ashift = SPA_MINBLOCKSHIFT;
unsigned long vdev_object_store_physical_ashift = SPA_MINBLOCKSHIFT;
struct sockaddr_un zfs_root_socket = {
	AF_UNIX, "/etc/zfs/zfs_root_socket"
};

typedef enum {
	VOS_SOCK_CLOSED = (1 << 0),
	VOS_SOCK_SHUTTING_DOWN = (1 << 1),
	VOS_SOCK_SHUTDOWN = (1 << 2),
	VOS_SOCK_OPENING = (1 << 3),
	VOS_SOCK_OPEN = (1 << 4),
	VOS_SOCK_READY = (1 << 5)
} socket_state_t;

typedef enum {
	VOS_TXG_BEGIN = 0,
	VOS_TXG_END,
	VOS_TXG_END_AGAIN,
	VOS_TXG_NONE
} vos_serial_flag_t;

typedef enum {
	VOS_SERIAL_CREATE_POOL,
	VOS_SERIAL_OPEN_POOL,
	VOS_SERIAL_END_TXG,
	VOS_SERIAL_CLOSE_POOL,
	VOS_SERIAL_ENABLE_FEATURE,
	VOS_SERIAL_TYPES
} vos_serial_types_t;

/*
 * Per request private data
 */
typedef struct vdev_object_store_request {
	uint64_t vosr_req;
} vdev_object_store_request_t;

typedef struct object_store_free_block {
	list_node_t osfb_list_node;
	uint64_t osfb_offset;
	uint64_t osfb_size;
} object_store_free_block_t;


typedef struct vdev_object_store {
	vdev_t *vos_vdev;
	char *vos_endpoint;
	char *vos_region;
	char *vos_cred_profile;
	kthread_t *vos_agent_thread;
	kmutex_t vos_lock;
	kcondvar_t vos_cv;
	boolean_t vos_agent_thread_exit;

	kmutex_t vos_stats_lock;
	vdev_object_store_stats_t vos_stats;

	kmutex_t vos_sock_lock;
	kcondvar_t vos_sock_cv;
	ksocket_t vos_sock;
	socket_state_t vos_sock_state;

	kmutex_t vos_outstanding_lock;
	kcondvar_t vos_outstanding_cv;
	boolean_t vos_serial_done[VOS_SERIAL_TYPES];
	vos_serial_flag_t vos_send_txg_selector;
	uint64_t vos_result;

	uint64_t vos_next_block;
	uberblock_t vos_uberblock;
	nvlist_t *vos_config;
	uint64_t vos_flush_point;

	list_t vos_free_list;
} vdev_object_store_t;

static mode_t
vdev_object_store_open_mode(spa_mode_t spa_mode)
{
	mode_t mode = 0;

	if ((spa_mode & SPA_MODE_READ) && (spa_mode & SPA_MODE_WRITE)) {
		mode = O_RDWR;
	} else if (spa_mode & SPA_MODE_READ) {
		mode = O_RDONLY;
	} else {
		panic("unknown spa mode");
	}

	return (mode);
}

static inline vdev_object_store_request_t *
vdev_object_store_request_alloc(void)
{
	return (kmem_zalloc(
	    sizeof (vdev_object_store_request_t), KM_SLEEP));
}

static void
vdev_object_store_request_free(zio_t *zio)
{
	/*
	 * Per request private data cleanup.
	 */
}

static const zio_vsd_ops_t vdev_object_store_vsd_ops = {
	.vsd_free = vdev_object_store_request_free,
};

static void
zfs_object_store_wait(vdev_object_store_t *vos, socket_state_t state)
{
	ASSERT(MUTEX_HELD(&vos->vos_sock_lock));
	ASSERT(MUTEX_NOT_HELD(&vos->vos_outstanding_lock));
	while (vos->vos_sock_state < state) {
		cv_wait(&vos->vos_sock_cv, &vos->vos_sock_lock);
	}
}

static int
zfs_object_store_open(vdev_object_store_t *vos)
{
	ksocket_t s = INVALID_SOCKET;

	ASSERT(MUTEX_HELD(&vos->vos_sock_lock));
	vos->vos_sock_state = VOS_SOCK_OPENING;
	int rc = ksock_create(PF_UNIX, SOCK_STREAM, 0, &s);
	if (rc != 0) {
		zfs_dbgmsg("zfs_object_store_open unable to create "
		    "socket: %d", rc);
		return (rc);
	}

	rc = ksock_connect(s, (struct sockaddr *)&zfs_root_socket,
	    sizeof (zfs_root_socket));
	if (rc != 0) {
		zfs_dbgmsg("zfs_object_store_open failed to "
		    "connect: %d", rc);
		ksock_close(s);
		s = INVALID_SOCKET;
	} else {
		zfs_dbgmsg("zfs_object_store_open, socket connection "
		    "ready, " SOCK_FMT, s);
	}

	VERIFY3P(vos->vos_sock, ==, INVALID_SOCKET);
	vos->vos_sock = s;
	zfs_dbgmsg("SOCKET OPEN(%px): " SOCK_FMT, curthread, vos->vos_sock);
	if (vos->vos_sock != INVALID_SOCKET) {
		vos->vos_sock_state = VOS_SOCK_OPEN;
		cv_broadcast(&vos->vos_sock_cv);
	}
	return (0);
}

static void
zfs_object_store_shutdown(vdev_object_store_t *vos)
{
	ASSERT(MUTEX_HELD(&vos->vos_sock_lock));
	if (vos->vos_sock == INVALID_SOCKET) {
		return;
	}

	zfs_dbgmsg("SOCKET SHUTTING DOWN(%px): " SOCK_FMT, curthread,
	    vos->vos_sock);
	vos->vos_sock_state = VOS_SOCK_SHUTTING_DOWN;
	ksock_shutdown(vos->vos_sock, SHUT_RDWR);
	vos->vos_sock_state = VOS_SOCK_SHUTDOWN;
}

static void
zfs_object_store_close(vdev_object_store_t *vos)
{
	ASSERT(MUTEX_HELD(&vos->vos_sock_lock));
	if (vos->vos_sock == INVALID_SOCKET) {
		return;
	}

	zfs_dbgmsg("SOCKET CLOSING(%px): " SOCK_FMT, curthread, vos->vos_sock);
	ksock_close(vos->vos_sock);
	vos->vos_sock = INVALID_SOCKET;
	vos->vos_sock_state = VOS_SOCK_CLOSED;
}

static int
agent_request(vdev_object_store_t *vos, nvlist_t *nv, char *tag)
{
	spa_t *spa = vos->vos_vdev->vdev_spa;

	ASSERT(MUTEX_HELD(&vos->vos_sock_lock));

	struct msghdr msg = {};
	kvec_t iov[2] = {};
	size_t iov_size = 0;
	char *iov_buf = fnvlist_pack(nv, &iov_size);
	uint64_t size64 = iov_size;
	if (zfs_flags & ZFS_DEBUG_OBJECT_STORE) {
		zfs_dbgmsg("sending %llu-byte request to agent type=%s",
		    (u_longlong_t)size64,
		    fnvlist_lookup_string(nv, AGENT_TYPE));
	}

	iov[0].iov_base = &size64;
	iov[0].iov_len = sizeof (size64);
	iov[1].iov_base = iov_buf;
	iov[1].iov_len = iov_size;
	uint64_t total_size = sizeof (size64) + iov_size;

	if (vos->vos_sock_state < VOS_SOCK_OPEN) {
		return (SET_ERROR(ENOTCONN));
	}

	if (zio_injection_enabled) {
		zfs_dbgmsg("%s INJECTION prior to send", tag);
		zio_handle_panic_injection(spa, tag, 1);
	}

	size_t sent = ksock_send(vos->vos_sock, &msg, iov,
	    2, total_size);
	if (sent != total_size) {
		zfs_dbgmsg("sent wrong length to agent socket: "
		    "expected %d got %d, closing socket",
		    (int)total_size, (int)sent);

		/*
		 * If we were unable to send, then the kernel
		 * will shutdown the socket and allow the resume
		 * logic to re-establish the connection and retry
		 * any operations which were in flight prior to this
		 * failure.
		 */
		zfs_object_store_shutdown(vos);
		VERIFY3U(vos->vos_sock_state, ==, VOS_SOCK_SHUTDOWN);
		zfs_object_store_close(vos);
		ASSERT3P(vos->vos_sock, ==, INVALID_SOCKET);
		VERIFY3U(vos->vos_sock_state, ==, VOS_SOCK_CLOSED);
	}

	if (zio_injection_enabled) {
		zfs_dbgmsg("%s INJECTION after send", tag);
		zio_handle_panic_injection(spa, tag, 2);
	}
	fnvlist_pack_free(iov_buf, iov_size);

	return (sent == total_size ? 0 : SET_ERROR(EINTR));
}

static int
agent_request_serial(vdev_object_store_t *vos, nvlist_t *nv, char *tag,
    vos_serial_types_t wait_type)
{
	ASSERT(!vos->vos_serial_done[wait_type]);
	return (agent_request(vos, nv, tag));
}

/*
 * Send request to agent; nvlist may be modified.
 */
static void
agent_request_zio(vdev_object_store_t *vos, zio_t *zio, nvlist_t *nv)
{
	ASSERT(MUTEX_HELD(&vos->vos_sock_lock));

	vdev_t *vd = vos->vos_vdev;
	vdev_object_store_request_t *vosr = zio->io_vsd;
	vdev_queue_t *vq = &vd->vdev_queue;
	uint64_t blockid = zio->io_offset >> SPA_MINBLOCKSHIFT;

	mutex_enter(&vq->vq_lock);
	vdev_queue_pending_add(vq, zio);
	mutex_exit(&vq->vq_lock);

	fnvlist_add_uint64(nv, AGENT_REQUEST_ID, blockid);
	fnvlist_add_uint64(nv, AGENT_TOKEN, (uint64_t)zio);
	vosr->vosr_req = blockid;
	if (zfs_flags & ZFS_DEBUG_OBJECT_STORE) {
		zfs_dbgmsg("agent_request_zio(blockid=%llu)",
		    (u_longlong_t)blockid);
	}

	agent_request(vos, nv, FTAG);
}

static zio_t *
agent_complete_zio(vdev_object_store_t *vos, uint64_t blockid,
    uintptr_t token)
{
	vdev_t *vd = vos->vos_vdev;
	vdev_queue_t *vq = &vd->vdev_queue;

	mutex_enter(&vq->vq_lock);
	zio_t *zio = avl_find(&vq->vq_active_tree, (zio_t *)token, NULL);
	VERIFY3P(zio, !=, NULL);
	VERIFY3P(zio, ==, token);
	VERIFY3U(zio->io_offset >> SPA_MINBLOCKSHIFT, ==, blockid);

	vdev_queue_pending_remove(vq, zio);
	vdev_object_store_request_t *vosr = zio->io_vsd;
	VERIFY3U(vosr->vosr_req, ==, blockid);
	mutex_exit(&vq->vq_lock);

	return (zio);
}

/*
 * Wait for a one-at-a-time operation to complete
 * (pool create, pool open, txg end). If there was an
 * error with the socket, threads will wait here and we will
 * retry the operation.
 */
static void
agent_wait_serial(vdev_object_store_t *vos, vos_serial_types_t wait_type)
{
	mutex_enter(&vos->vos_outstanding_lock);
	while (!vos->vos_serial_done[wait_type])
		cv_wait(&vos->vos_outstanding_cv, &vos->vos_outstanding_lock);
	vos->vos_serial_done[wait_type] = B_FALSE;
	mutex_exit(&vos->vos_outstanding_lock);
}

static nvlist_t *
agent_io_block_alloc(zio_t *zio)
{
	uint64_t blockid = zio->io_offset >> SPA_MINBLOCKSHIFT;
	nvlist_t *nv = fnvlist_alloc();

	if (zio->io_type == ZIO_TYPE_WRITE) {
		fnvlist_add_string(nv, AGENT_TYPE, AGENT_TYPE_WRITE_BLOCK);
		void *buf = abd_borrow_buf_copy(zio->io_abd, zio->io_size);
		fnvlist_add_uint8_array(nv, AGENT_DATA, buf, zio->io_size);
		abd_return_buf(zio->io_abd, buf, zio->io_size);
	} else {
		ASSERT3U(zio->io_type, ==, ZIO_TYPE_READ);
		fnvlist_add_string(nv, AGENT_TYPE, AGENT_TYPE_READ_BLOCK);
	}
	fnvlist_add_uint64(nv, AGENT_SIZE, zio->io_size);
	fnvlist_add_uint64(nv, AGENT_BLKID, blockid);

	if ((zio->io_flags & ZIO_FLAG_IO_RETRY) ||
	    (zio->io_flags & ZIO_FLAG_SCRUB)) {
		fnvlist_add_boolean_value(nv, AGENT_HEAL, B_TRUE);
	}

	if (zfs_flags & ZFS_DEBUG_OBJECT_STORE) {
		zfs_dbgmsg("agent_io_block_alloc(guid=%llu blkid=%llu "
		    "len=%llu) %s",
		    (u_longlong_t)spa_guid(zio->io_spa), (u_longlong_t)blockid,
		    (u_longlong_t)zio->io_size,
		    zio->io_type == ZIO_TYPE_WRITE ? "WRITE" : "READ");
	}
	return (nv);
}

static inline void
agent_io_block_free(nvlist_t *nv)
{
	fnvlist_free(nv);
}

void
object_store_restart_agent(vdev_t *vd)
{
	vdev_object_store_t *vos = vd->vdev_tsd;
	ASSERT(MUTEX_HELD(&vos->vos_sock_lock));
	/*
	 * We need to ensure that we only issue a request when the
	 * socket is ready. Otherwise, we block here since the agent
	 * might be in recovery.
	 */
	zfs_object_store_wait(vos, VOS_SOCK_OPEN);

	nvlist_t *nv = fnvlist_alloc();
	/*
	 * XXX This doesn't actually exit the agent, it just tells the agent to
	 * close the connection.  We could just as easily close the connection
	 * ourself.  Or change the agent code to actually exit.
	 */
	fnvlist_add_string(nv, AGENT_TYPE, AGENT_TYPE_EXIT);
	agent_request(vos, nv, FTAG);
	fnvlist_free(nv);
}

/*
 * XXX This doesn't actually stop the agent, it just tells the agent to close
 * the pool (practically, to mark the pool as no longer owned by this agent).
 */
static void
object_store_stop_agent(vdev_t *vd)
{
	vdev_object_store_t *vos = vd->vdev_tsd;
	if (vos->vos_sock == INVALID_SOCKET)
		return;

	spa_t *spa = vd->vdev_spa;
	boolean_t destroy = spa_state(spa) == POOL_STATE_DESTROYED;

	ASSERT(MUTEX_HELD(&vos->vos_sock_lock));
	/*
	 * We need to ensure that we only issue a request when the
	 * socket is ready. Otherwise, we block here since the agent
	 * might be in recovery.
	 */
	zfs_dbgmsg("stop_agent() destroy=%d", destroy);
	zfs_object_store_wait(vos, VOS_SOCK_OPEN);

	// Tell agent to destroy if needed.

	nvlist_t *nv = fnvlist_alloc();
	fnvlist_add_string(nv, AGENT_TYPE, AGENT_TYPE_CLOSE_POOL);
	fnvlist_add_boolean_value(nv, AGENT_DESTROY, destroy);
	agent_request_serial(vos, nv, FTAG, VOS_SERIAL_CLOSE_POOL);
	fnvlist_free(nv);
	agent_wait_serial(vos, VOS_SERIAL_CLOSE_POOL);
}

static int
agent_free_blocks(vdev_object_store_t *vos)
{
	ASSERT(MUTEX_HELD(&vos->vos_sock_lock));

	int blocks_freed = 0;
	for (object_store_free_block_t *osfb = list_head(&vos->vos_free_list);
	    osfb != NULL; osfb = list_next(&vos->vos_free_list, osfb)) {

		blocks_freed++;
		uint64_t blockid = osfb->osfb_offset >> 9;
		nvlist_t *nv = fnvlist_alloc();
		fnvlist_add_string(nv, AGENT_TYPE, AGENT_TYPE_FREE_BLOCK);

		fnvlist_add_uint64(nv, AGENT_BLKID, blockid);
		fnvlist_add_uint64(nv, AGENT_SIZE, osfb->osfb_size);
		if (zfs_flags & ZFS_DEBUG_OBJECT_STORE) {
			zfs_dbgmsg("agent_free_blocks(blkid=%llu, asize=%llu)",
			    (u_longlong_t)blockid,
			    (u_longlong_t)osfb->osfb_size);
		}
		int err = agent_request(vos, nv, FTAG);
		if (err != 0) {
			fnvlist_free(nv);
			zfs_dbgmsg("agnet_free_block failed to send: %d", err);
			return (err);
		}
		fnvlist_free(nv);
	}
	zfs_dbgmsg("agent_free_blocks freed %d blocks", blocks_freed);
	return (0);
}

static void
agent_create_pool(vdev_t *vd, vdev_object_store_t *vos)
{
	/*
	 * We need to ensure that we only issue a request when the
	 * socket is ready. Otherwise, we block here since the agent
	 * might be in recovery.
	 */
	mutex_enter(&vos->vos_sock_lock);
	zfs_object_store_wait(vos, VOS_SOCK_OPEN);

	nvlist_t *nv = fnvlist_alloc();
	fnvlist_add_string(nv, AGENT_TYPE, AGENT_TYPE_CREATE_POOL);
	fnvlist_add_string(nv, AGENT_NAME, spa_name(vd->vdev_spa));
	fnvlist_add_uint64(nv, AGENT_GUID, spa_guid(vd->vdev_spa));
	if (vos->vos_cred_profile != NULL) {
		fnvlist_add_string(nv, AGENT_CRED_PROFILE,
		    vos->vos_cred_profile);
	}
	fnvlist_add_string(nv, AGENT_ENDPOINT, vos->vos_endpoint);
	fnvlist_add_string(nv, AGENT_REGION, vos->vos_region);
	fnvlist_add_string(nv, AGENT_BUCKET, vd->vdev_path);
	zfs_dbgmsg("agent_create_pool(guid=%llu name=%s bucket=%s)",
	    (u_longlong_t)spa_guid(vd->vdev_spa),
	    spa_name(vd->vdev_spa),
	    vd->vdev_path);
	agent_request_serial(vos, nv, FTAG, VOS_SERIAL_CREATE_POOL);

	mutex_exit(&vos->vos_sock_lock);
	fnvlist_free(nv);
	agent_wait_serial(vos, VOS_SERIAL_CREATE_POOL);
}

static uint64_t
agent_open_pool(vdev_t *vd, vdev_object_store_t *vos, mode_t mode,
    boolean_t resume)
{
	/*
	 * We need to ensure that we only issue a request when the
	 * socket is ready. Otherwise, we block here since the agent
	 * might be in recovery.
	 */
	mutex_enter(&vos->vos_sock_lock);
	zfs_object_store_wait(vos, VOS_SOCK_OPEN);

	nvlist_t *nv = fnvlist_alloc();
	fnvlist_add_string(nv, AGENT_TYPE, AGENT_TYPE_OPEN_POOL);
	fnvlist_add_uint64(nv, AGENT_GUID, spa_guid(vd->vdev_spa));
	if (vos->vos_cred_profile != NULL) {
		fnvlist_add_string(nv, AGENT_CRED_PROFILE,
		    vos->vos_cred_profile);
	}
	fnvlist_add_string(nv, AGENT_ENDPOINT, vos->vos_endpoint);
	fnvlist_add_string(nv, AGENT_REGION, vos->vos_region);
	fnvlist_add_string(nv, AGENT_BUCKET, vd->vdev_path);
	fnvlist_add_boolean_value(nv, AGENT_RESUME, resume);
	if (mode == O_RDONLY)
		fnvlist_add_boolean(nv, AGENT_READONLY);
	if (vd->vdev_spa->spa_load_max_txg != UINT64_MAX) {
		fnvlist_add_uint64(nv, AGENT_TXG,
		    vd->vdev_spa->spa_load_max_txg);
	}
	zfs_dbgmsg("agent_open_pool(guid=%llu bucket=%s)",
	    (u_longlong_t)spa_guid(vd->vdev_spa),
	    vd->vdev_path);
	agent_request_serial(vos, nv, FTAG, VOS_SERIAL_OPEN_POOL);

	mutex_exit(&vos->vos_sock_lock);
	fnvlist_free(nv);
	agent_wait_serial(vos, VOS_SERIAL_OPEN_POOL);
	return (vos->vos_result);
}

static void
agent_begin_txg(vdev_object_store_t *vos, uint64_t txg)
{
	ASSERT(MUTEX_HELD(&vos->vos_sock_lock));
	zfs_object_store_wait(vos, VOS_SOCK_READY);

	nvlist_t *nv = fnvlist_alloc();
	fnvlist_add_string(nv, AGENT_TYPE, AGENT_TYPE_BEGIN_TXG);
	fnvlist_add_uint64(nv, AGENT_TXG, txg);
	zfs_dbgmsg("agent_begin_txg(%llu)",
	    (u_longlong_t)txg);

	agent_request(vos, nv, FTAG);
	fnvlist_free(nv);
}

static void
agent_resume_txg(vdev_object_store_t *vos, uint64_t txg)
{
	ASSERT(MUTEX_HELD(&vos->vos_sock_lock));
	zfs_object_store_wait(vos, VOS_SOCK_OPEN);

	nvlist_t *nv = fnvlist_alloc();
	fnvlist_add_string(nv, AGENT_TYPE, AGENT_TYPE_RESUME_TXG);
	fnvlist_add_uint64(nv, AGENT_TXG, txg);

	zfs_dbgmsg("agent_resume_txg(%llu)",
	    (u_longlong_t)txg);
	agent_request(vos, nv, FTAG);
	fnvlist_free(nv);
}

static void
agent_resume_complete(vdev_object_store_t *vos)
{
	ASSERT(MUTEX_HELD(&vos->vos_sock_lock));
	zfs_object_store_wait(vos, VOS_SOCK_OPEN);

	nvlist_t *nv = fnvlist_alloc();
	fnvlist_add_string(nv, AGENT_TYPE, AGENT_TYPE_RESUME_COMPLETE);

	zfs_dbgmsg("agent_resume_complete()");
	agent_request(vos, nv, FTAG);
	fnvlist_free(nv);
}

static void
agent_end_txg(vdev_object_store_t *vos, uint64_t txg, void *ub_buf,
    size_t ub_len, void *config_buf, size_t config_len)
{
	ASSERT(MUTEX_HELD(&vos->vos_sock_lock));
	/*
	 * External consumers need to wait until the connection has
	 * reached a ready state. However, when we are doing recovery
	 * we only need to be in the open state, so we check that here.
	 */
	zfs_object_store_wait(vos, VOS_SOCK_OPEN);

	nvlist_t *nv = fnvlist_alloc();
	fnvlist_add_string(nv, AGENT_TYPE, AGENT_TYPE_END_TXG);
	fnvlist_add_uint64(nv, AGENT_TXG, txg);
	fnvlist_add_uint8_array(nv, AGENT_UBERBLOCK, ub_buf, ub_len);
	fnvlist_add_uint8_array(nv, AGENT_CONFIG, config_buf, config_len);

	zfs_dbgmsg("agent_end_txg(%llu), %u passes",
	    (u_longlong_t)txg,
	    vos->vos_vdev->vdev_spa->spa_sync_pass);
	agent_request_serial(vos, nv, FTAG, VOS_SERIAL_END_TXG);
	fnvlist_free(nv);
}

static void
agent_flush_writes(vdev_object_store_t *vos, uint64_t blockid)
{
	mutex_enter(&vos->vos_sock_lock);
	zfs_object_store_wait(vos, VOS_SOCK_READY);

	nvlist_t *nv = fnvlist_alloc();
	fnvlist_add_string(nv, AGENT_TYPE, AGENT_TYPE_FLUSH_WRITES);
	fnvlist_add_uint64(nv, AGENT_BLKID, blockid);
	zfs_dbgmsg("agent_flush: blockid %llu", (u_longlong_t)blockid);

	agent_request(vos, nv, FTAG);
	mutex_exit(&vos->vos_sock_lock);
	fnvlist_free(nv);
}

static void
agent_set_feature(vdev_object_store_t *vos, zfeature_info_t *feature)
{
	mutex_enter(&vos->vos_sock_lock);
	zfs_object_store_wait(vos, VOS_SOCK_READY);

	nvlist_t *nv = fnvlist_alloc();
	fnvlist_add_string(nv, AGENT_TYPE, AGENT_TYPE_ENABLE_FEATURE);
	fnvlist_add_string(nv, AGENT_FEATURE, feature->fi_guid);
	zfs_dbgmsg("agent_set_feature: feature %s", feature->fi_guid);

	/*
	 * We do a serial operation here because we need to make sure that a
	 * response is waited for before we proceed with the txg and
	 * potentially finish it. This may be better suited for the upcoming
	 * token-based approach planned for iostat.
	 */
	agent_request_serial(vos, nv, FTAG, VOS_SERIAL_ENABLE_FEATURE);
	mutex_exit(&vos->vos_sock_lock);
	fnvlist_free(nv);
	agent_wait_serial(vos, VOS_SERIAL_ENABLE_FEATURE);
}

static int
agent_resume_state_check(vdev_t *vd)
{
	vdev_object_store_t *vos = vd->vdev_tsd;

	/*
	 * If we're resuming in the middle of pool creation,
	 * then the agent may not have any on-disk state yet.
	 * We wait till after TXG_INITIAL to ensure that
	 * the agent has fully processed our initial transaction
	 * group.
	 */
	if (vd->vdev_spa->spa_load_state == SPA_LOAD_CREATE &&
	    vd->vdev_spa->spa_uberblock.ub_txg <= TXG_INITIAL) {
		return (0);
	}

	if (bcmp(&vd->vdev_spa->spa_ubsync, &vos->vos_uberblock,
	    sizeof (uberblock_t)) == 0) {
		return (0);
	}
	if (vos->vos_send_txg_selector == VOS_TXG_END) {
		/*
		 * In this case, it's possible that the uberblock was written
		 * out before we got the end txg done message. We can safely
		 * continue by sending the "end txg" command again, without
		 * doing "resume txg".
		 */
		if (bcmp(&vd->vdev_spa->spa_uberblock, &vos->vos_uberblock,
		    sizeof (uberblock_t)) == 0) {
			zfs_dbgmsg("resume: uberblock matches spa_uberblock; "
			    "calling TXG_END again");
			vos->vos_send_txg_selector = VOS_TXG_END_AGAIN;
			return (0);
		}
	}
	return (SET_ERROR(EBUSY));
}

static void
agent_resume(void *arg)
{
	vdev_t *vd = arg;
	vdev_object_store_t *vos = vd->vdev_tsd;
	spa_t *spa = vd->vdev_spa;
	int ret;

	zfs_dbgmsg("agent_resume running");

	/*
	 * Wait till the socket is opened.
	 */
	mutex_enter(&vos->vos_sock_lock);
	zfs_object_store_wait(vos, VOS_SOCK_OPEN);
	mutex_exit(&vos->vos_sock_lock);

	/*
	 * Re-establish the connection with the agent and send
	 * open/create message.
	 */
	if (spa->spa_load_state == SPA_LOAD_CREATE) {
		agent_create_pool(vd, vos);
	}
	VERIFY0(agent_open_pool(vd, vos,
	    vdev_object_store_open_mode(spa_mode(vd->vdev_spa)), B_TRUE));

	if ((ret = agent_resume_state_check(vd)) != 0) {
		zfs_dbgmsg("agent resume failed, uberblock changed");
		vdev_set_state(vd, B_FALSE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_MODIFIED);
		vos->vos_agent_thread_exit = B_TRUE;
		return;
	}

	mutex_enter(&vos->vos_sock_lock);

	if (vos->vos_send_txg_selector <= VOS_TXG_END) {
		agent_resume_txg(vos, spa_syncing_txg(spa));
	}

	vdev_queue_t *vq = &vd->vdev_queue;

	mutex_enter(&vq->vq_lock);
	for (zio_t *zio = avl_first(&vq->vq_active_tree); zio != NULL;
	    zio = AVL_NEXT(&vq->vq_active_tree, zio)) {
		uint64_t req = zio->io_offset >> SPA_MINBLOCKSHIFT;
		vdev_object_store_request_t *vosr = zio->io_vsd;
		VERIFY3U(vosr->vosr_req, ==, req);

		/*
		 * If we're at END state then we shouldn't have
		 * any outstanding writes in the queue.
		 */
		if (vos->vos_send_txg_selector == VOS_TXG_END) {
			VERIFY3U(zio->io_type, !=, ZIO_TYPE_WRITE);
		}

		nvlist_t *nv = agent_io_block_alloc(zio);
		fnvlist_add_uint64(nv, AGENT_REQUEST_ID, req);
		fnvlist_add_uint64(nv, AGENT_TOKEN, (uint64_t)zio);
		zfs_dbgmsg("ZIO REISSUE (%px) req %llu",
		    zio, (u_longlong_t)req);
		if ((ret = agent_request(vos, nv, FTAG)) != 0) {
			zfs_dbgmsg("agent_resume failed: %d", ret);
			agent_io_block_free(nv);
			vos->vos_agent_thread_exit = B_TRUE;
			mutex_exit(&vq->vq_lock);
			mutex_exit(&vos->vos_sock_lock);
			return;
		}
		agent_io_block_free(nv);
	}
	mutex_exit(&vq->vq_lock);
	if (vos->vos_send_txg_selector <= VOS_TXG_END) {
		agent_resume_complete(vos);
	}

	/*
	 * We only free blocks if we haven't written
	 * out the uberblock.
	 */
	if (vos->vos_send_txg_selector == VOS_TXG_END &&
	    agent_free_blocks(vos) != 0)  {
		zfs_dbgmsg("agent_resume freeing failed");
		mutex_exit(&vos->vos_sock_lock);
		return;
	}

	if (vos->vos_send_txg_selector == VOS_TXG_END ||
	    vos->vos_send_txg_selector == VOS_TXG_END_AGAIN) {
		size_t nvlen;
		char *nvbuf = fnvlist_pack(vos->vos_config, &nvlen);
		agent_end_txg(vos, spa_syncing_txg(spa),
		    &spa->spa_uberblock, sizeof (spa->spa_uberblock),
		    nvbuf, nvlen);
		fnvlist_pack_free(nvbuf, nvlen);
	}

	/*
	 * Once we've reissued all pending I/Os, mark the socket
	 * as ready for use so that normal communication can
	 * continue.
	 */
	vos->vos_sock_state = VOS_SOCK_READY;
	cv_broadcast(&vos->vos_sock_cv);
	mutex_exit(&vos->vos_sock_lock);

	zfs_dbgmsg("agent_resume completed");
}

void
object_store_begin_txg(vdev_t *vd, uint64_t txg)
{
	ASSERT(vdev_is_object_based(vd));
	vdev_object_store_t *vos = vd->vdev_tsd;
	ASSERT(vos->vos_send_txg_selector == VOS_TXG_NONE);
	mutex_enter(&vos->vos_sock_lock);
	agent_begin_txg(vos, txg);
	vos->vos_send_txg_selector = VOS_TXG_BEGIN;
	mutex_exit(&vos->vos_sock_lock);
}

static void
remove_cred_profile(nvlist_t *config)
{
	nvlist_t *tree;
	char *profile;

	tree = fnvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE);
	if (nvlist_lookup_string(tree,
	    ZPOOL_CONFIG_CRED_PROFILE, &profile) == 0) {
		(void) nvlist_remove_all(tree, ZPOOL_CONFIG_CRED_PROFILE);
	}
}

void
object_store_end_txg(vdev_t *vd, nvlist_t *config, uint64_t txg)
{
	spa_t *spa = vd->vdev_spa;
	ASSERT(vdev_is_object_based(vd));
	vdev_object_store_t *vos = vd->vdev_tsd;
	mutex_enter(&vos->vos_sock_lock);
	/*
	 * We need to ensure that we only issue a request when the
	 * socket is ready. Otherwise, we block here since the agent
	 * might be in recovery.
	 */
	zfs_object_store_wait(vos, VOS_SOCK_READY);

	// The credentials profile should not be persisted on-disk.
	remove_cred_profile(config);

	vos->vos_send_txg_selector = VOS_TXG_END;
	if (agent_free_blocks(vos) == 0)  {
		size_t nvlen;
		char *nvbuf = fnvlist_pack(config, &nvlen);
		agent_end_txg(vos, txg,
		    &spa->spa_uberblock, sizeof (spa->spa_uberblock),
		    nvbuf, nvlen);
		fnvlist_pack_free(nvbuf, nvlen);

		if (vos->vos_config != NULL)
			fnvlist_free(vos->vos_config);
		vos->vos_config = fnvlist_dup(config);
	}

	mutex_exit(&vos->vos_sock_lock);
	agent_wait_serial(vos, VOS_SERIAL_END_TXG);

	object_store_free_block_t *osfb;
	while ((osfb = list_remove_head(&vos->vos_free_list)) != NULL) {
		kmem_free(osfb, sizeof (object_store_free_block_t));
	}
	ASSERT(list_is_empty(&vos->vos_free_list));
	vos->vos_send_txg_selector = VOS_TXG_NONE;
}

void
object_store_free_block(vdev_t *vd, uint64_t offset, uint64_t asize)
{
	ASSERT(vdev_is_object_based(vd));
	vdev_object_store_t *vos = vd->vdev_tsd;

	/*
	 * We add freed blocks to our list which will get processed
	 * at the end of the txg.
	 */
	object_store_free_block_t *osfb =
	    kmem_alloc(sizeof (object_store_free_block_t),
	    KM_SLEEP);
	osfb->osfb_offset = offset;
	osfb->osfb_size = asize;
	list_insert_tail(&vos->vos_free_list, osfb);
}

void
object_store_flush_writes(spa_t *spa, uint64_t offset)
{
	vdev_t *vd = spa->spa_root_vdev->vdev_child[0];
	ASSERT(vdev_is_object_based(vd));
	vdev_object_store_t *vos = vd->vdev_tsd;
	uint64_t blockid = offset >> SPA_MINBLOCKSHIFT;
	agent_flush_writes(vos, blockid);
}

void
object_store_get_stats(vdev_t *vd, vdev_object_store_stats_t *vossp)
{
	ASSERT(vdev_is_object_based(vd));
	vdev_object_store_t *vos = vd->vdev_tsd;

	mutex_enter(&vos->vos_stats_lock);
	*vossp = vos->vos_stats;
	mutex_exit(&vos->vos_stats_lock);
}

static void
update_features(spa_t *spa, nvlist_t *nv)
{
	for (nvpair_t *elem = nvlist_next_nvpair(nv, NULL);
	    elem != NULL; elem = nvlist_next_nvpair(nv, elem)) {
		spa_feature_t feat;
		if (zfeature_lookup_guid(nvpair_name(elem), &feat))
			continue;

		spa->spa_feat_refcount_cache[feat] = fnvpair_value_uint64(elem);
	}
}

static int
agent_read_all(vdev_object_store_t *vos, void *buf, size_t len)
{
	size_t recvd_total = 0;
	while (recvd_total < len) {
		struct msghdr msg = {};
		kvec_t iov = {};

		iov.iov_base = buf + recvd_total;
		iov.iov_len = len - recvd_total;

		mutex_enter(&vos->vos_lock);
		if (vos->vos_agent_thread_exit ||
		    vos->vos_sock == INVALID_SOCKET) {
			zfs_dbgmsg("(%px) agent_read_all shutting down",
			    curthread);
			mutex_exit(&vos->vos_lock);
			return (SET_ERROR(ENOTCONN));
		}

		mutex_exit(&vos->vos_lock);

		size_t recvd = ksock_receive(vos->vos_sock,
		    &msg, &iov, 1, len - recvd_total, 0);
		if (recvd > 0) {
			recvd_total += recvd;
			if (recvd_total < len &&
			    (zfs_flags & ZFS_DEBUG_OBJECT_STORE)) {
				zfs_dbgmsg("incomplete recvmsg but trying for "
				    "more len=%d recvd=%d recvd_total=%d",
				    (int)len,
				    (int)recvd,
				    (int)recvd_total);
			}
		} else {
			zfs_dbgmsg("got wrong length from agent socket: "
			    "for total size %d, already received %d, "
			    "expected up to %d got %d",
			    (int)len,
			    (int)recvd_total,
			    (int)(len - recvd_total),
			    (int)recvd);
			/* XXX - Do we need to check for errors too? */
			if (recvd == 0)
				return (SET_ERROR(EAGAIN));
		}
	}
	return (0);
}

static int
agent_reader(void *arg)
{
	vdev_object_store_t *vos = arg;
	uint64_t nvlist_len = 0;
	int err = agent_read_all(vos, &nvlist_len, sizeof (nvlist_len));
	if (err != 0) {
		zfs_dbgmsg("agent_reader(%px) got err %d", curthread, err);
		return (err);
	}

	void *buf = vmem_alloc(nvlist_len, KM_SLEEP);
	err = agent_read_all(vos, buf, nvlist_len);
	if (err != 0) {
		zfs_dbgmsg("2 agent_reader(%px) got err %d", curthread, err);
		vmem_free(buf, nvlist_len);
		return (err);
	}

	nvlist_t *nv;
	err = nvlist_unpack(buf, nvlist_len, &nv, KM_SLEEP);
	vmem_free(buf, nvlist_len);
	if (err != 0) {
		zfs_dbgmsg("got error %d from nvlist_unpack(len=%d)",
		    err, (int)nvlist_len);
		return (EAGAIN);
	}

	const char *type = fnvlist_lookup_string(nv, AGENT_TYPE);
	if (zfs_flags & ZFS_DEBUG_OBJECT_STORE) {
		zfs_dbgmsg("got response from agent type=%s", type);
	}
	// XXX debug message the nvlist
	if (strcmp(type, AGENT_TYPE_CREATE_POOL_DONE) == 0) {
		mutex_enter(&vos->vos_outstanding_lock);
		ASSERT(!vos->vos_serial_done[VOS_SERIAL_CREATE_POOL]);
		vos->vos_serial_done[VOS_SERIAL_CREATE_POOL] = B_TRUE;
		cv_broadcast(&vos->vos_outstanding_cv);
		mutex_exit(&vos->vos_outstanding_lock);
	} else if (strcmp(type, AGENT_TYPE_END_TXG_DONE) == 0) {
		mutex_enter(&vos->vos_stats_lock);
		vos->vos_stats.voss_blocks_count =
		    fnvlist_lookup_uint64(nv, "blocks_count");
		uint64_t old_blocks_bytes = vos->vos_stats.voss_blocks_bytes;
		vos->vos_stats.voss_blocks_bytes =
		    fnvlist_lookup_uint64(nv, "blocks_bytes");
		int64_t alloc_delta =
		    vos->vos_stats.voss_blocks_bytes - old_blocks_bytes;
		vos->vos_stats.voss_pending_frees_count =
		    fnvlist_lookup_uint64(nv, "pending_frees_count");
		vos->vos_stats.voss_pending_frees_bytes =
		    fnvlist_lookup_uint64(nv, "pending_frees_bytes");
		vos->vos_stats.voss_objects_count =
		    fnvlist_lookup_uint64(nv, "objects_count");
		/*
		 * vos->vos_vdev->vdev_stat.vs_alloc =
		 *  vos->vos_stats.voss_blocks_bytes;
		 */
		mutex_exit(&vos->vos_stats_lock);

		metaslab_space_update(vos->vos_vdev,
		    vos->vos_vdev->vdev_spa->spa_normal_class,
		    alloc_delta, 0, 0);

		update_features(vos->vos_vdev->vdev_spa,
		    fnvlist_lookup_nvlist(nv, AGENT_FEATURES));

		mutex_enter(&vos->vos_outstanding_lock);
		ASSERT(!vos->vos_serial_done[VOS_SERIAL_END_TXG]);
		vos->vos_serial_done[VOS_SERIAL_END_TXG] = B_TRUE;
		cv_broadcast(&vos->vos_outstanding_cv);
		mutex_exit(&vos->vos_outstanding_lock);
	} else if (strcmp(type, AGENT_TYPE_OPEN_POOL_DONE) == 0) {
		uint_t len;
		uint8_t *arr;
		int err = nvlist_lookup_uint8_array(nv, AGENT_UBERBLOCK,
		    &arr, &len);
		if (err == 0) {
			ASSERT3U(len, ==, sizeof (uberblock_t));
			bcopy(arr, &vos->vos_uberblock, len);
			VERIFY0(nvlist_lookup_uint8_array(nv,
			    AGENT_CONFIG, &arr, &len));
			vos->vos_config = fnvlist_unpack((char *)arr, len);

			update_features(vos->vos_vdev->vdev_spa,
			    fnvlist_lookup_nvlist(nv, AGENT_FEATURES));
		}

		uint64_t next_block = fnvlist_lookup_uint64(nv,
		    AGENT_NEXT_BLOCK);
		vos->vos_next_block = next_block;

		zfs_dbgmsg("got pool open done len=%u block=%llu",
		    len, (u_longlong_t)next_block);

		fnvlist_free(nv);
		mutex_enter(&vos->vos_outstanding_lock);
		ASSERT(!vos->vos_serial_done[VOS_SERIAL_OPEN_POOL]);
		vos->vos_serial_done[VOS_SERIAL_OPEN_POOL] = B_TRUE;
		cv_broadcast(&vos->vos_outstanding_cv);
		mutex_exit(&vos->vos_outstanding_lock);
	} else if (strcmp(type, AGENT_TYPE_OPEN_POOL_FAILED) == 0) {
		char *cause = fnvlist_lookup_string(nv, AGENT_CAUSE);
		spa_t *spa = vos->vos_vdev->vdev_spa;
		zfs_dbgmsg("got %s cause=\"%s\"", type, cause);
		if (strcmp(cause, "MMP") == 0) {
			fnvlist_add_string(spa->spa_load_info,
			    ZPOOL_CONFIG_MMP_HOSTNAME, fnvlist_lookup_string(nv,
			    AGENT_HOSTNAME));
			fnvlist_add_uint64(spa->spa_load_info,
			    ZPOOL_CONFIG_MMP_STATE, MMP_STATE_ACTIVE);
			fnvlist_add_uint64(spa->spa_load_info,
			    ZPOOL_CONFIG_MMP_TXG, 0);
			mutex_enter(&vos->vos_outstanding_lock);
			vos->vos_result = SET_ERROR(EREMOTEIO);
		} else if (strcmp(cause, "IO") == 0) {
			mutex_enter(&vos->vos_outstanding_lock);
			if (strstr(cause, "does not exist") != NULL) {
				vos->vos_result = SET_ERROR(ENOENT);
			} else {
				vos->vos_result = SET_ERROR(EIO);
			}
		} else {
			ASSERT0(strcmp(cause, "feature"));
			fnvlist_add_nvlist(spa->spa_load_info,
			    ZPOOL_CONFIG_UNSUP_FEAT, fnvlist_lookup_nvlist(nv,
			    AGENT_FEATURES));
			if (fnvlist_lookup_boolean_value(nv,
			    AGENT_CAN_READONLY)) {
				fnvlist_add_boolean(spa->spa_load_info,
				    ZPOOL_CONFIG_CAN_RDONLY);
			}

			mutex_enter(&vos->vos_outstanding_lock);
			vos->vos_result = SET_ERROR(ENOTSUP);
		}

		ASSERT(!vos->vos_serial_done[VOS_SERIAL_OPEN_POOL]);
		vos->vos_serial_done[VOS_SERIAL_OPEN_POOL] = B_TRUE;
		cv_broadcast(&vos->vos_outstanding_cv);
		mutex_exit(&vos->vos_outstanding_lock);
		fnvlist_free(nv);
	} else if (strcmp(type, AGENT_TYPE_READ_DONE) == 0) {
		uint64_t req = fnvlist_lookup_uint64(nv,
		    AGENT_REQUEST_ID);
		uintptr_t token = fnvlist_lookup_uint64(nv, AGENT_TOKEN);
		uint_t len;
		void *data = fnvlist_lookup_uint8_array(nv,
		    AGENT_DATA, &len);
		if (zfs_flags & ZFS_DEBUG_OBJECT_STORE) {
			zfs_dbgmsg("got read done req=%llu datalen=%u, "
			    "token %px",
			    (u_longlong_t)req, len, (zio_t *)token);
		}
		zio_t *zio = agent_complete_zio(vos, req, token);
		VERIFY3U(fnvlist_lookup_uint64(nv, AGENT_BLKID), ==,
		    zio->io_offset >> SPA_MINBLOCKSHIFT);
		VERIFY3U(len, ==, zio->io_size);
		VERIFY3U(len, ==, abd_get_size(zio->io_abd));
		abd_copy_from_buf(zio->io_abd, data, len);
		fnvlist_free(nv);
		zio_delay_interrupt(zio);
	} else if (strcmp(type, AGENT_TYPE_WRITE_DONE) == 0) {
		uint64_t req = fnvlist_lookup_uint64(nv,
		    AGENT_REQUEST_ID);
		uintptr_t token = fnvlist_lookup_uint64(nv, AGENT_TOKEN);
		if (zfs_flags & ZFS_DEBUG_OBJECT_STORE) {
			zfs_dbgmsg("got write done req=%llu, token %px",
			    (u_longlong_t)req, (zio_t *)token);
		}
		zio_t *zio = agent_complete_zio(vos, req, token);
		VERIFY3U(fnvlist_lookup_uint64(nv, AGENT_BLKID), ==,
		    zio->io_offset >> SPA_MINBLOCKSHIFT);
		fnvlist_free(nv);
		zio_delay_interrupt(zio);
	} else if (strcmp(type, AGENT_TYPE_CLOSE_POOL_DONE) == 0) {
		zfs_dbgmsg("got %s", type);
		mutex_enter(&vos->vos_outstanding_lock);
		ASSERT(!vos->vos_serial_done[VOS_SERIAL_CLOSE_POOL]);
		vos->vos_serial_done[VOS_SERIAL_CLOSE_POOL] = B_TRUE;
		cv_broadcast(&vos->vos_outstanding_cv);
		mutex_exit(&vos->vos_outstanding_lock);
		mutex_enter(&vos->vos_lock);
		vos->vos_agent_thread_exit = B_TRUE;
		mutex_exit(&vos->vos_lock);
	} else if (strcmp(type, AGENT_TYPE_ENABLE_FEATURE_DONE) == 0) {
		mutex_enter(&vos->vos_outstanding_lock);
		ASSERT(!vos->vos_serial_done[VOS_SERIAL_ENABLE_FEATURE]);
		vos->vos_serial_done[VOS_SERIAL_ENABLE_FEATURE] = B_TRUE;
		cv_broadcast(&vos->vos_outstanding_cv);
		mutex_exit(&vos->vos_outstanding_lock);
	} else {
		zfs_dbgmsg("unrecognized response type!");
	}
	return (0);
}

static int
vdev_object_store_socket_open(vdev_t *vd)
{
	vdev_object_store_t *vos = vd->vdev_tsd;

	/*
	 * XXX - We open the socket continuously waiting
	 * for the agent to start accepting connections.
	 * We may need to provide a mechanism to break out and
	 * fail the import instead.
	 */
	while (!vos->vos_agent_thread_exit &&
	    vos->vos_sock == INVALID_SOCKET) {

		mutex_enter(&vos->vos_lock);
		VERIFY3P(vos->vos_sock, ==, INVALID_SOCKET);

		mutex_enter(&vos->vos_sock_lock);
		int error = zfs_object_store_open(vos);
		mutex_exit(&vos->vos_sock_lock);
		if (error != 0) {
			mutex_exit(&vos->vos_lock);
			return (error);
		}

		if (vos->vos_sock == INVALID_SOCKET) {
			delay(hz);
		} else {
			cv_broadcast(&vos->vos_cv);
		}

		mutex_exit(&vos->vos_lock);
	}
	return (0);
}

static void
vdev_agent_thread(void *arg)
{
	vdev_t *vd = arg;
	vdev_object_store_t *vos = vd->vdev_tsd;

	while (!vos->vos_agent_thread_exit) {

		int err = agent_reader(vos);
		if (vos->vos_agent_thread_exit || err == 0)
			continue;

		/*
		 * The agent has crashed so we need to start recovery.
		 * We first need to shutdown the socket. Manipulating
		 * the socket requires consumers to hold the vosr_sock_lock
		 * which also protects the vosr_sock_state.
		 *
		 * Once the socket is shutdown, no other thread should
		 * be able to send or receive on that socket. We also need
		 * to wakeup any threads that are currently waiting for a
		 * serial request.
		 */

		zfs_dbgmsg("(%px) agent_reader exited, reopen, err %d",
		    curthread, err);

		mutex_enter(&vos->vos_sock_lock);
		zfs_object_store_shutdown(vos);
		VERIFY3U(vos->vos_sock_state, <=, VOS_SOCK_SHUTDOWN);

		/*
		 * XXX - it's possible that the socket may reopen
		 * immediately because the connection is not completely
		 * closed by the server. To prevent this, we delay here.
		 */
		delay(hz);

		zfs_object_store_close(vos);
		mutex_exit(&vos->vos_sock_lock);
		ASSERT3P(vos->vos_sock, ==, INVALID_SOCKET);
		VERIFY3U(vos->vos_sock_state, ==, VOS_SOCK_CLOSED);

		vdev_object_store_socket_open(vd);
		zfs_dbgmsg("REOPENED(%px) sock " SOCK_FMT, curthread,
		    vos->vos_sock);

		/* XXX - make sure we only run this once and it completes */
		VERIFY3U(taskq_dispatch(system_taskq,
		    agent_resume, vd, TQ_SLEEP), !=, TASKQID_INVALID);
	}

	mutex_enter(&vos->vos_lock);
	vos->vos_agent_thread = NULL;
	cv_broadcast(&vos->vos_cv);
	mutex_exit(&vos->vos_lock);
	zfs_dbgmsg("agent thread exited");
	thread_exit();
}

static int
vdev_object_store_init(spa_t *spa, nvlist_t *nv, void **tsd)
{
	vdev_object_store_t *vos;
	char *val = NULL;

	vos = *tsd = kmem_zalloc(sizeof (vdev_object_store_t), KM_SLEEP);
	vos->vos_sock = INVALID_SOCKET;
	vos->vos_vdev = NULL;
	vos->vos_send_txg_selector = VOS_TXG_NONE;
	vos->vos_flush_point = -1ULL;
	mutex_init(&vos->vos_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&vos->vos_stats_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&vos->vos_sock_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&vos->vos_outstanding_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&vos->vos_cv, NULL, CV_DEFAULT, NULL);
	cv_init(&vos->vos_sock_cv, NULL, CV_DEFAULT, NULL);
	cv_init(&vos->vos_outstanding_cv, NULL, CV_DEFAULT, NULL);

	list_create(&vos->vos_free_list, sizeof (object_store_free_block_t),
	    offsetof(object_store_free_block_t, osfb_list_node));

	if (!nvlist_lookup_string(nv,
	    zpool_prop_to_name(ZPOOL_PROP_OBJ_ENDPOINT), &val)) {
		vos->vos_endpoint = kmem_strdup(val);
	} else {
		return (SET_ERROR(EINVAL));
	}
	if (!nvlist_lookup_string(nv,
	    zpool_prop_to_name(ZPOOL_PROP_OBJ_REGION), &val)) {
		vos->vos_region = kmem_strdup(val);
	} else {
		return (SET_ERROR(EINVAL));
	}
	if (!nvlist_lookup_string(nv, ZPOOL_CONFIG_CRED_PROFILE, &val)) {
		vos->vos_cred_profile = kmem_strdup(val);
	}

	zfs_dbgmsg("vdev_object_store_init, endpoint=%s region=%s profile=%s",
	    vos->vos_endpoint, vos->vos_region, vos->vos_cred_profile);

	return (0);
}

static void
vdev_object_store_fini(vdev_t *vd)
{
	vdev_object_store_t *vos = vd->vdev_tsd;

	ASSERT(list_is_empty(&vos->vos_free_list));
	list_destroy(&vos->vos_free_list);
	mutex_destroy(&vos->vos_lock);
	mutex_destroy(&vos->vos_stats_lock);
	mutex_destroy(&vos->vos_sock_lock);
	mutex_destroy(&vos->vos_outstanding_lock);
	cv_destroy(&vos->vos_cv);
	cv_destroy(&vos->vos_sock_cv);
	cv_destroy(&vos->vos_outstanding_cv);
	if (vos->vos_endpoint != NULL) {
		kmem_strfree(vos->vos_endpoint);
	}
	if (vos->vos_region != NULL) {
		kmem_strfree(vos->vos_region);
	}
	if (vos->vos_cred_profile != NULL) {
		kmem_strfree(vos->vos_cred_profile);
	}
	if (vos->vos_config != NULL) {
		fnvlist_free(vos->vos_config);
	}
	kmem_free(vd->vdev_tsd, sizeof (vdev_object_store_t));
	vd->vdev_tsd = NULL;

	zfs_dbgmsg("vdev_object_store_fini");
}

static int
vdev_object_store_open(vdev_t *vd, uint64_t *psize, uint64_t *max_psize,
    uint64_t *logical_ashift, uint64_t *physical_ashift)
{
	vdev_object_store_t *vos;
	int error = 0;

	/*
	 * Rotational optimizations only make sense on block devices.
	 */
	vd->vdev_nonrot = B_TRUE;

	/*
	 * Allow TRIM on object store based vdevs.  This may not always
	 * be supported, since it depends on your kernel version and
	 * underlying filesystem type but it is always safe to attempt.
	 */
	vd->vdev_has_trim = B_FALSE;

	/*
	 * Disable secure TRIM on object store based vdevs.
	 */
	vd->vdev_has_securetrim = B_FALSE;

	/*
	 * We use the pathname to specfiy the object store name.
	 */
	if (vd->vdev_path == NULL) {
		vd->vdev_stat.vs_aux = VDEV_AUX_BAD_LABEL;
		return (SET_ERROR(EINVAL));
	}

	vos = vd->vdev_tsd;
	vos->vos_vdev = vd;

	/*
	 * Reopen the device if it's not currently open.  Otherwise,
	 * just update the physical size of the device.
	 */
	if (vd->vdev_reopening) {
		goto skip_open;
	}
	ASSERT(vd->vdev_path != NULL);
	ASSERT3P(vos->vos_agent_thread, ==, NULL);

	error = vdev_object_store_socket_open(vd);

	/* XXX - this can't happen today */
	if (error) {
		vd->vdev_stat.vs_aux = VDEV_AUX_OPEN_FAILED;
		return (error);
	}

	vos->vos_agent_thread = thread_create(NULL, 0, vdev_agent_thread,
	    vd, 0, &p0, TS_RUN, defclsyspri);

	if (vd->vdev_spa->spa_load_state == SPA_LOAD_CREATE) {
		agent_create_pool(vd, vos);
	}
	error = agent_open_pool(vd, vos,
	    vdev_object_store_open_mode(spa_mode(vd->vdev_spa)), B_FALSE);
	if (error != 0) {
		ASSERT3U(vd->vdev_spa->spa_load_state, !=, SPA_LOAD_CREATE);
		return (error);
	}

	/*
	 * Socket is now ready for communication, wake up
	 * anyone waiting.
	 */
	mutex_enter(&vos->vos_sock_lock);
	vos->vos_sock_state = VOS_SOCK_READY;
	cv_broadcast(&vos->vos_sock_cv);
	mutex_exit(&vos->vos_sock_lock);

skip_open:

	/*
	 * XXX - We can only support ~1EB since the metaslab weights
	 * use some of the high order bits.
	 */
	*max_psize = *psize = (1ULL << 60) - 1;
	*logical_ashift = vdev_object_store_logical_ashift;
	*physical_ashift = vdev_object_store_physical_ashift;

	return (0);
}

static void
vdev_object_store_close(vdev_t *vd)
{
	vdev_object_store_t *vos = vd->vdev_tsd;

	if (vd->vdev_reopening || vos == NULL)
		return;

	mutex_enter(&vos->vos_sock_lock);
	object_store_stop_agent(vd);
	mutex_exit(&vos->vos_sock_lock);

	mutex_enter(&vos->vos_lock);
	vos->vos_agent_thread_exit = B_TRUE;
	vos->vos_vdev = NULL;

	mutex_enter(&vos->vos_sock_lock);
	zfs_object_store_shutdown(vos);
	mutex_exit(&vos->vos_sock_lock);

	while (vos->vos_agent_thread != NULL) {
		zfs_dbgmsg("vdev_object_store_close: shutting down agent");
		cv_wait(&vos->vos_cv, &vos->vos_lock);
	}

	mutex_enter(&vos->vos_sock_lock);
	zfs_object_store_close(vos);
	mutex_exit(&vos->vos_sock_lock);

	mutex_exit(&vos->vos_lock);
	ASSERT3P(vos->vos_sock, ==, INVALID_SOCKET);
	vd->vdev_delayed_close = B_FALSE;
}

static void
vdev_object_store_io_start(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	vdev_object_store_t *vos = vd->vdev_tsd;

	if (zio->io_type == ZIO_TYPE_IOCTL) {
		/* XXPOLICY */
		if (!vdev_readable(vd)) {
			zio->io_error = SET_ERROR(ENXIO);
			zio_interrupt(zio);
			return;
		}

		switch (zio->io_cmd) {
		case DKIOCFLUSHWRITECACHE:

			if (zfs_nocacheflush)
				break;

			/*
			 * XXX - may need a new ioctl sinc this will
			 * sync the entire object store.
			 */
			break;
		default:
			zio->io_error = SET_ERROR(ENOTSUP);
		}

		zio_execute(zio);
		return;
	} else if (zio->io_type == ZIO_TYPE_TRIM) {
		/* XXX - Don't support it right now */
		zio->io_error = SET_ERROR(ENOTSUP);
		zio_execute(zio);
		return;
	}

	zio->io_vsd = vdev_object_store_request_alloc();
	zio->io_vsd_ops = &vdev_object_store_vsd_ops;

	nvlist_t *nv = agent_io_block_alloc(zio);

	/*
	 * We need to ensure that we only issue a request when the
	 * socket is ready. Otherwise, we block here since the agent
	 * might be in recovery.
	 */
	mutex_enter(&vos->vos_sock_lock);
	zfs_object_store_wait(vos, VOS_SOCK_READY);

	zio->io_target_timestamp = zio_handle_io_delay(zio);
	agent_request_zio(vos, zio, nv);
	mutex_exit(&vos->vos_sock_lock);

	agent_io_block_free(nv);
}

/* ARGSUSED */
static void
vdev_object_store_io_done(zio_t *zio)
{
}

static void
vdev_object_store_config_generate(vdev_t *vd, nvlist_t *nv)
{
	vdev_object_store_t *vos = vd->vdev_tsd;

	fnvlist_add_string(nv,
	    zpool_prop_to_name(ZPOOL_PROP_OBJ_ENDPOINT), vos->vos_endpoint);
	fnvlist_add_string(nv,
	    zpool_prop_to_name(ZPOOL_PROP_OBJ_REGION), vos->vos_region);
	if (vos->vos_cred_profile != NULL) {
		fnvlist_add_string(nv, ZPOOL_CONFIG_CRED_PROFILE,
		    vos->vos_cred_profile);
	}
}

static void
vdev_object_store_metaslab_init(vdev_t *vd, metaslab_t *msp,
    uint64_t *ms_start, uint64_t *ms_size)
{
	vdev_object_store_t *vos = vd->vdev_tsd;
	msp->ms_lbas[0] = vos->vos_next_block;
}

/*
 * Lockout allocations and find highest allocated block.
 */
uint64_t
vdev_object_store_metaslab_offset(vdev_t *vd)
{
	boolean_t lock_held = spa_config_held(vd->vdev_spa,
	    SCL_ALLOC, RW_WRITER);
	if (!lock_held)
		spa_config_enter(vd->vdev_spa, SCL_ALLOC, FTAG, RW_WRITER);

	uint64_t blockid = 0;
	for (uint64_t m = 0; m < vd->vdev_ms_count; m++) {
		metaslab_t *msp = vd->vdev_ms[m];
		blockid = MAX(blockid, msp->ms_lbas[0]);
	}

	if (!lock_held)
		spa_config_exit(vd->vdev_spa, SCL_ALLOC, FTAG);

	/*
	 * The blockid represents the next block that will be allocated
	 * so we need to subtract one to get the last allocated block
	 * and then convert it to an offset.
	 */
	return (blockid > 0 ? (blockid - 1) << SPA_MINBLOCKSHIFT: 0);
}


uberblock_t *
vdev_object_store_get_uberblock(vdev_t *vd)
{
	ASSERT(vdev_is_object_based(vd) && vd->vdev_ops->vdev_op_leaf);
	vdev_object_store_t *vos = vd->vdev_tsd;
	return (&vos->vos_uberblock);
}

nvlist_t *
vdev_object_store_get_config(vdev_t *vd)
{
	vdev_object_store_t *vos = vd->vdev_tsd;
	return (fnvlist_dup(vos->vos_config));
}

static void
vdev_object_store_enable_feature(vdev_t *vd, zfeature_info_t *zfeature)
{
	agent_set_feature(vd->vdev_tsd, zfeature);
}

/*
 * This function defines the flush point that will be use whenever
 * the SCL_ZIO spa_config_lock is obtained as writer. Any write
 * that is grabbing the SCL_ZIO spa_confg_lock as reader will not
 * block if the allocated block it is issuing is less than or equal
 * to that flush point. This is required since the agent must
 * be told when to flush writes to the backend and must receive
 * all blocks up to that point.
 *
 * Once the flush point is established, we notify the agent and
 * then use that value as a way to allow in-flight writes to
 * "passthru" the normal spa_config_lock semantics. This means
 * spa_config_log writers will be starved momentarily while we finish
 * issuing writes to the agent.
 */
void
vdev_object_store_enable_passthru(vdev_t *vd)
{
	for (int c = 0; c < vd->vdev_children; c++) {
		vdev_object_store_enable_passthru(vd->vdev_child[c]);
	}

	if (vd->vdev_ops->vdev_op_leaf && vdev_is_object_based(vd)) {
		ASSERT3P(vd, ==, vd->vdev_top);
		vdev_object_store_t *vos = vd->vdev_tsd;

		/*
		 * Get the highest offset that we've allocated.
		 */
		uint64_t offset = vdev_object_store_metaslab_offset(vd);

		mutex_enter(&vos->vos_lock);
		vos->vos_flush_point = offset;
		mutex_exit(&vos->vos_lock);

		zfs_dbgmsg("flush point set to %llu",
		    (u_longlong_t)vos->vos_flush_point);
		object_store_flush_writes(vd->vdev_spa, vos->vos_flush_point);
	}
}

/*
 * Return the established flush point or -1ULL if one is does not exist.
 * Note, the flush point may be for blockid in the past, which is fine.
 */
uint64_t
vdev_object_store_flush_point(vdev_t *vd)
{
	for (int c = 0; c < vd->vdev_children; c++) {
		vdev_t *cvd = vd->vdev_child[c];
		if (cvd->vdev_islog || cvd->vdev_aux != NULL)
			continue;

		if (vdev_is_object_based(cvd)) {
			ASSERT3P(cvd, ==, cvd->vdev_top);
			ASSERT(cvd->vdev_ops->vdev_op_leaf);
			vdev_object_store_t *vos = cvd->vdev_tsd;
			return (vos->vos_flush_point);
		}
	}
	return (-1ULL);
}

vdev_ops_t vdev_object_store_ops = {
	.vdev_op_init = vdev_object_store_init,
	.vdev_op_fini = vdev_object_store_fini,
	.vdev_op_open = vdev_object_store_open,
	.vdev_op_close = vdev_object_store_close,
	.vdev_op_asize = vdev_default_asize,
	.vdev_op_min_asize = vdev_default_min_asize,
	.vdev_op_min_alloc = NULL,
	.vdev_op_io_start = vdev_object_store_io_start,
	.vdev_op_io_done = vdev_object_store_io_done,
	.vdev_op_state_change = NULL,
	.vdev_op_need_resilver = NULL,
	.vdev_op_hold = NULL,
	.vdev_op_rele = NULL,
	.vdev_op_remap = NULL,
	.vdev_op_xlate = vdev_default_xlate,
	.vdev_op_rebuild_asize = NULL,
	.vdev_op_metaslab_init = vdev_object_store_metaslab_init,
	.vdev_op_config_generate = vdev_object_store_config_generate,
	.vdev_op_nparity = NULL,
	.vdev_op_ndisks = NULL,
	.vdev_op_enable_feature = vdev_object_store_enable_feature,
	.vdev_op_type = VDEV_TYPE_OBJSTORE,	/* name of this vdev type */
	.vdev_op_leaf = B_TRUE			/* leaf vdev */
};

ZFS_MODULE_PARAM(zfs_vdev_object_store, vdev_object_store_,
    logical_ashift, ULONG, ZMOD_RW,
	"Logical ashift for object store based devices");
ZFS_MODULE_PARAM(zfs_vdev_object_store, vdev_object_store_,
    physical_ashift, ULONG, ZMOD_RW,
	"Physical ashift for object store based devices");
