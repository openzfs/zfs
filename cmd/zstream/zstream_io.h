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

/*
 * Copyright (c) 2026 by Garth Snyder. All rights reserved.
 */

#ifndef _ZSTREAM_IO_H
#define	_ZSTREAM_IO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/zfs_ioctl.h>

#include "zstream_chain.h"

#define	MAX_IO_STREAMS		4
#define	MAX_DROP_FILTERS	4

/*
 * Masks for serial_drop_record_types()
 */

#define	DROP_BEGIN		(UINT32_C(1) << DRR_BEGIN)
#define	DROP_OBJECT		(UINT32_C(1) << DRR_OBJECT)
#define	DROP_FREEOBJECTS	(UINT32_C(1) << DRR_FREEOBJECTS)
#define	DROP_WRITE		(UINT32_C(1) << DRR_WRITE)
#define	DROP_FREE		(UINT32_C(1) << DRR_FREE)
#define	DROP_END		(UINT32_C(1) << DRR_END)
#define	DROP_WRITE_BYREF	(UINT32_C(1) << DRR_WRITE_BYREF)
#define	DROP_SPILL		(UINT32_C(1) << DRR_SPILL)
#define	DROP_WRITE_EMBEDDED	(UINT32_C(1) << DRR_WRITE_EMBEDDED)
#define	DROP_OBJECT_RANGE	(UINT32_C(1) << DRR_OBJECT_RANGE)
#define	DROP_REDACT		(UINT32_C(1) << DRR_REDACT)

/*
 * The stream offset is the offset within the original source stream.
 * Changes to the stream (e.g., recompression) will necessarily change
 * offsets within the final stream. The original stream offset is raw data;
 * it should never be updated.
 */
typedef struct {
	dmu_replay_record_t	dp_drr;
	uint8_t			*dp_payload;
	uint32_t		dp_payload_size;
	off_t			dp_stream_offset;
	boolean_t		dp_cksum_resync;
} drr_packet_t;

/*
 * In the following, the filename or checkpoint names must remain valid
 * as long as the chain is executing.
 */

chain_step_t
serial_read_stream(const char *filename);

/*
 * A skip function is shown each record header that follows the DRR_BEGIN
 * record, in native byte order, until it first returns B_FALSE. Records for
 * which it returns B_TRUE are discarded by seeking past them, without reading
 * their payloads, provided the input is seekable. It must not depend on
 * anything other than the header and its argument, as it runs in the reader
 * ahead of the rest of the chain.
 *
 * The last record skipped is not discarded. It is re-read and emitted with
 * dp_cksum_resync set, so that checksum validation can take up the stream
 * checksum from its header. Downstream steps must still discard it.
 */
typedef boolean_t (*skip_func_t)(const dmu_replay_record_t *drr, void *arg);

chain_step_t
serial_read_stream_skip(const char *filename, skip_func_t skip, void *arg);

chain_step_t
serial_write_stream(const char *filename);

/*
 * When payloads change (e.g., after being decompressed), this function
 * should always be used to intermediate. It frees the old payload and
 * updates the accounting for total data in flight. To free the old payload
 * without replacing it, just pass in NULL.
 */
void
set_payload(void *item_in, void *payload, uint64_t size);

/*
 * Sometimes, e.g., in the implementation of "zstream raw", we want to take
 * a payload buffer out of the chain system and hand its control over to some
 * other system. We need to update the memory accounting but not attempt to
 * free the buffer.
 */
void
export_payload(void *item_in);

/*
 * Report throughput periodically
 */
chain_step_t
serial_checkpoint(const char *name);

/*
 * Winnow the stream by dropping records of the given types. This frees up
 * payload memory used by records you won't be inspecting. If there are
 * parallel operations downstream of the filter, removing records allows the
 * parallel queues to be used more efficiently.
 *
 * Use the DROP_* defines to construct a mask of the records you want to
 * remove. If you want to remove most records, it's fine to pass an inverted
 * mask formed by enumerating only the records you want to keep, e.g.:
 *
 * serial_drop_record_types((uint32_t)~(DROP_WRITE | DROP_WRITE_EMBEDDED))
 *
 * This step should be placed downstream of byteswapping, since it relies on
 * being able to read drr->drr_type.
 */
chain_step_t
serial_drop_record_types(uint32_t drop_mask);

/*
 * Usually the output step is responsible for freeing payloads. Subcommands
 * that don't have stream outputs still need to free this memory. A
 * serial_null_output step does this and nothing more.
 */
chain_step_t
serial_null_output(void);

/* Off-the-shelf zstream_queue cost functions */

size_t
constant_cost_of_one(queue_item_t *packet, void *context);

size_t
payload_size_as_cost(queue_item_t *packet, void *context);

#ifdef __cplusplus
}
#endif

#endif  /* _ZSTREAM_IO_H */
