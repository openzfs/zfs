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
 * Copyright (c) 2025 Jorgen Lundman <lundman@lundman.net>.
 */

#ifndef OPENZFS_PIPE_RPC_H
#define	OPENZFS_PIPE_RPC_H

#include <stdint.h>

#undef dprintf
static void
dprintf(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf_s(buf, sizeof (buf), _TRUNCATE, fmt, ap);
	va_end(ap);
	OutputDebugStringA(buf);
}

#define	OPENZFS_PIPE_NAME "\\\\.\\pipe\\openzfs_zed"

#pragma pack(push, 1)
typedef enum {
    OP_GET_STATUS = 1,
    OP_LIST_POOLS = 2,
    OP_IMPORT_ALL = 3,
    OP_IMPORT_SCAN = 4,
    OP_IMPORT_ONE = 5,
    OP_SUBSCRIBE_EVENTS = 6,
    OP_EXPORT_ALL = 7,
    OP_EXPORT_ONE = 8,
    OP_MOUNT_POOL = 9,
    OP_UNMOUNT_POOL = 10,
    OP_MOUNT_PREFLIGHT = 11,
    OP_LOAD_KEY_ONE = 12,
} op_t;

typedef struct {
    uint32_t op; // op_t
    uint32_t len; // payload length in bytes (follows header)
} req_hdr_t;

typedef struct {
    uint32_t status; // 0 == OK, else Win32-style or your own
    uint32_t len; // payload length in bytes (follows header)
} rsp_hdr_t;
#pragma pack(pop)

// pipe_rpc.h
typedef enum {
    ZFSV_SUMMARY = 0,  // name/health/size/alloc/free/capacity
    ZFSV_INCLUDE_VDEVS = 1, // + vdev_tree
} zfs_status_verbosity_t;

#pragma pack(push, 1)
typedef struct {
    uint8_t  verbosity; // zfs_status_verbosity_t
    uint8_t  reserved[3];
    uint64_t guid; // target pool GUID
} op_get_status_by_guid_req_t;
#pragma pack(pop)

enum {
    ZIMP_FORCE = 0x01, // zpool import -f
    ZIMP_READONLY = 0x02, // readonly=on
    ZIMP_NOMOUNT = 0x04, // -N
    ZIMP_LOADKEYS = 0x08, // -l
};

#pragma pack(push, 1)
typedef struct {
	uint32_t flags; // ZIMP_*
	// followed by optional UTF-8 altroot (NUL-terminated) in the tail
} op_import_all_req_t;

typedef struct {
	uint32_t flags; // ZIMP_* (can influence scan heuristics)
	// future: search paths, cachefile, pool name filter
} op_import_scan_req_t;

typedef struct {
	uint32_t flags; // ZIMP_*
	uint64_t guid; // which pool to import
	// followed by optional UTF-8 new_name (NUL-terminated)
	// followed by optional UTF-8 altroot (NUL-terminated)
} op_import_one_req_t;
#pragma pack(pop)

enum {
    ZEXP_FORCE = 0x01, // zpool export -f  (force unmount datasets)
    ZEXP_HARD = 0x02, // optional: hard force if your tree supports it
};

#pragma pack(push, 1)
typedef struct {
    uint32_t flags; // ZEXP_*
} op_export_all_req_t;

typedef struct {
    uint32_t flags; // ZEXP_*
    uint64_t guid; // pool to export (we’ll resolve to a handle)
} op_export_one_req_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    uint32_t flags; // ZMNT_*
    uint64_t pool_guid; // or 0 + pool_name[]
    char pool_name[128]; // optional if you prefer names
    char mntopts[256]; // optional comma-delimited, or "" (NULL ok too)
} op_mount_req_t;

typedef struct {
    uint32_t flags; // ZUMNT_FORCE etc.
    uint64_t pool_guid;
    char pool_name[128];
} op_unmount_req_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
    uint32_t flags; // reserved
    uint64_t pool_guid; // 0 if using name
    char pool_name[128]; // UTF-8
    char dataset[512]; // leave empty for pool-scope
} op_mount_preflight_req_t;

typedef struct {
    uint32_t flags; // reserved
    char dataset[512]; // UTF-8 encroot name
    uint32_t passlen; // trailing bytes
} op_load_key_one_req_t;
#pragma pack(pop)

// Writes header + optional payload. Frees payload if 'do_free' is true.
#define	RESP_EX(client, err, size_sz, payload_ptr, do_free) \
	do { \
		const void *__resp_pl = (payload_ptr); \
		uint32_t __resp_sz_sz = (size_sz); \
		uint32_t __resp_sz = (uint32_t)((__resp_sz_sz > 0xFFFFFFFFu) ? \
		    0xFFFFFFFFu : __resp_sz_sz); \
		rsp_hdr_t __rsp = { (uint32_t)(err), __resp_sz }; \
		WriteAll((client), &__rsp, sizeof (__rsp)); \
		if (__resp_sz && __resp_pl) \
			WriteAll((client), __resp_pl, __resp_sz); \
		if ((do_free) && __resp_pl) { \
			HeapFree(GetProcessHeap(), 0, (void*)__resp_pl); \
		} \
	} while (0)

// Common cases:
#define	RESP_OK_JSON(client, size_sz, payload_ptr) do { \
		RESP_EX(client, 0, (size_sz), (payload_ptr), TRUE); \
		(payload_ptr) = NULL; \
	} while (0)

#define	RESP_ERR(client, win32err) RESP_EX(client, (win32err), 0, NULL, FALSE)

#endif // OPENZFS_PIPE_RPC_H
