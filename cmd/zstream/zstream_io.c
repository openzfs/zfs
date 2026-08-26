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

#include <arpa/inet.h>
#include <err.h>
#include <libzutil.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/byteorder.h>
#include <sys/stdtypes.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/zfs_ioctl.h>
#include <time.h>
#include <unistd.h>

#include "zstream_chain.h"
#include "zstream_modules.h"
#include "zstream_util.h"

/*
 * Memory devoted to storing payloads is limited to
 *
 *   MEMORY_BASE + (system_memory - MEMORY_BASE_CUTOFF) * MEMORY_PCT / 100
 *
 * When memory is exhausted, chain_read() waits until memory in use is
 * MEMORY_HYSTERESIS bytes lower than the nominal limit.
 */
#define	MEMORY_BASE		(512 << 20)	/* 512MB */
#define	MEMORY_BASE_CUTOFF	(4ULL << 30)	/* 4GB */
#define	MEMORY_PCT		10		/* % beyond the base region */
#define	MEMORY_HYSTERESIS 	(128 << 20)	/* 128MB */

/*
 * Init only the filename; chain functions will prepare the FILE *
 */
typedef struct {
	const char	*ic_filename;
	FILE		*ic_fp;
	boolean_t	ic_for_reading;
	off_t		ic_offset;
} io_context_t;

typedef struct {
	const char	*cc_name;
	double		cc_last_sec;
	double		cc_period_sec;
	uint64_t	cc_last_bytes;
} checkpoint_context_t;

/*
 * See comments at set_payload() for more information about locking and
 * performance considerations for data-in-flight tracking. Briefly, access
 * patterns make the mutex expensive, so it's reserved for awakening threads
 * that are waiting for memory.
 */
typedef struct {
	pthread_mutex_t	dif_mutex;
	pthread_cond_t	dif_cond;
	uint64_t	dif_current;	/* atomic access only */
	boolean_t	dif_waiting;	/* atomic access only */
	uint64_t	dif_allowed;
	uint64_t	dif_resume;	/* dif_allowed - MEMORY_HYSTERESIS */
} data_in_flight_t;

static io_context_t io_contexts[MAX_IO_STREAMS];
static int next_io_context = 0;

static checkpoint_context_t checkpoint_contexts[MAX_IO_STREAMS];
static int next_checkpoint_context = 0;

static uint32_t drop_contexts[MAX_DROP_FILTERS];
static int next_drop_context = 0;

static data_in_flight_t payloads = {
	.dif_mutex = PTHREAD_MUTEX_INITIALIZER,
	.dif_cond = PTHREAD_COND_INITIALIZER
};

static pthread_once_t	dif_init_control = PTHREAD_ONCE_INIT;

/*
 * Called through setup_io() -> pthread_once()
 */
static void
initialize_memory_tracking(void)
{
	ssize_t pagesize = (ssize_t)sysconf(_SC_PAGESIZE);
	ssize_t pages = (ssize_t)sysconf(_SC_PHYS_PAGES);
	if (pagesize < 0 || pages < 0) {
		warnx("unable to read system memory info");
		payloads.dif_allowed = UINT64_MAX; /* no limit */
	} else {
		uint64_t total_mem = (uint64_t)pagesize * (uint64_t)pages;
		int64_t flex = total_mem - MEMORY_BASE_CUTOFF;
		int64_t addl = (double)flex * MEMORY_PCT / 100;
		payloads.dif_allowed = MEMORY_BASE + MAX(addl, 0);
	}
	payloads.dif_resume = payloads.dif_allowed - MEMORY_HYSTERESIS;
}

/*
 * Run from within chain execution to initialize I/O. A NULL filename
 * indicates stdin or stdout.
 */
static void
open_file(io_context_t *context)
{
	if (context->ic_filename) {
		context->ic_fp = fopen(context->ic_filename,
		    context->ic_for_reading ? "rb" : "wb+");
		if (!context->ic_fp) {
			perror(context->ic_filename);
			exit(1);
		}
	} else if (context->ic_for_reading && isatty(STDIN_FILENO)) {
		errx(1, "stream cannot be read from a terminal. "
		    "Name a file or take input from a pipe.");
	} else if (context->ic_for_reading) {
		context->ic_fp = stdin;
	} else if (isatty(STDOUT_FILENO)) {
		errx(1, "stream cannot be written to a terminal. "
		    "Capture output to a file or pipe to another command.");
	} else {
		context->ic_fp = stdout;
	}
}

/*
 * Extract the payload size from a replay record that is potentially
 * byteswapped. We want to leave the bulk of byteswapping to another module,
 * so just take a quick, nondestructive peek.
 *
 * Record-specific macros such as DRR_WRITE_PAYLOAD_SIZE do not seem to be
 * byteswap-aware. However, with the exception of DRR_OBJECT_PAYLOAD_SIZE,
 * they happen to work with post-swapping since they are switching on either
 * a uint8_t value or 0.
 *
 * DRR_WRITE and DRR_SPILL use 64-bit sizes. The other two record types have
 * 32-bit sizes. The drr_payloadlen field shared by all record types (but
 * used only by BEGIN records is also 32 bits.
 */
static size_t
calc_payload_size(dmu_replay_record_t *drr)
{
	struct drr_object *drro		 = &drr->drr_u.drr_object;
	struct drr_write *drrw		 = &drr->drr_u.drr_write;
	struct drr_spill *drrs		 = &drr->drr_u.drr_spill;
	struct drr_write_embedded *drrwe = &drr->drr_u.drr_write_embedded;

	boolean_t swap = ATTR_IS_SET(CA_BYTESWAPPED);
	uint32_t drr_type = swap ? BSWAP_32(drr->drr_type) : drr->drr_type;
	uint64_t size, size64 = 0;
	uint32_t size32 = 0;
	boolean_t round = B_FALSE;

	if (drr_type == DRR_OBJECT) {
		round = drro->drr_raw_bonuslen == 0;
		size32 = round ? drro->drr_bonuslen : drro->drr_raw_bonuslen;
	} else if (drr_type == DRR_WRITE) {
		size64 = DRR_WRITE_PAYLOAD_SIZE(drrw);
	} else if (drr_type == DRR_SPILL) {
		size64 = DRR_SPILL_PAYLOAD_SIZE(drrs);
	} else if (drr_type == DRR_WRITE_EMBEDDED) {
		size32 = drrwe->drr_psize;
		round = B_TRUE;
	} else if (drr_type == DRR_BEGIN) {
		size32 = drr->drr_payloadlen;
	} else {
		return (0);
	}
	if (size32 != 0) {
		size = swap ? BSWAP_32(size32) : size32;
	} else {
		size = swap ? BSWAP_64(size64) : size64;
	}
	return (round ? P2ROUNDUP(size, 8) : size);
}

/*
 * Must be called only with the first record in a stream. Must be a
 * DRR_BEGIN record or we'll terminate with "invalid stream".
 */
static void
set_stream_attributes(drr_packet_t *item)
{
	dmu_replay_record_t *drr  = &item->dp_drr;
	struct drr_begin *drrb    = &drr->drr_u.drr_begin;
	uint64_t magic		  = drrb->drr_magic;
	uint64_t versioninfo	  = drrb->drr_versioninfo;
	boolean_t i_am_big_endian = htonl(0xFF00) == 0xFF00;

	boolean_t swap_on_output, is_deduped;

	if (magic == BSWAP_64(DMU_BACKUP_MAGIC)) {
		SET_ATTR(CA_BYTESWAPPED);
		versioninfo = BSWAP_64(versioninfo);
	} else if (magic != DMU_BACKUP_MAGIC) {
		errx(1, "invalid ZFS stream, bad magic number %llx",
		    (u_longlong_t)magic);
	}
	if (i_am_big_endian == ATTR_IS_SET(CA_BYTESWAPPED)) {
		SET_ATTR(CA_LITTLE_ENDIAN_INPUT);
	} else {
		SET_ATTR(CA_BIG_ENDIAN_INPUT);
	}
	chain_attrs->ca_feature_flags = DMU_GET_FEATUREFLAGS(versioninfo);

	is_deduped =
	    STREAM_HAS_FEATURE(DMU_BACKUP_FEATURE_DEDUP) ||
	    STREAM_HAS_FEATURE(DMU_BACKUP_FEATURE_DEDUPPROPS);

	if (OPTION_ENABLED(CA_FORBID_DEDUP) && is_deduped) {
		errx(1, "input stream is deduplicated, but this subcommand "
		    "does not support deduplicated streams. Use 'zstream "
		    "redup' to reduplicate.");
	}
	boolean_t req_dedup = OPTION_ENABLED(CA_REQUIRE_DEDUP);
	boolean_t is_dedup = STREAM_HAS_FEATURE(DMU_BACKUP_FEATURE_DEDUP);
	if (req_dedup && !is_dedup) {
		errx(1, "this subcommand requires a deduplicated input "
		    "stream, but the stream is not deduplicated");
	}
	boolean_t req_native = OPTION_ENABLED(CA_REQUIRE_NATIVE_ENDIAN);
	boolean_t is_byteswapped = ATTR_IS_SET(CA_BYTESWAPPED);
	if (req_native && is_byteswapped) {
		errx(1, "this subcommand requires a native-endian "
		    "input stream");
	}

	/*
	 * Figure out output endianness. In the absence of explicit byte
	 * order instructions, we default to preserving the input byte
	 * order. Record headers are always converted to native byte order
	 * for processing, but they can be swapped back on output.
	 *
	 * zfs receive inspects the endianness of each DRR record
	 * and assumes, at least in some cases, that payload data has the
	 * same order as the DMU wrappers.
	 */
	if (OPTION_ENABLED(CA_BIG_ENDIAN_OUT))
		swap_on_output = !i_am_big_endian;
	else if (OPTION_ENABLED(CA_LITTLE_ENDIAN_OUT))
		swap_on_output = i_am_big_endian;
	else if (OPTION_ENABLED(CA_OPPOSITE_ENDIAN_OUT))
		swap_on_output = !ATTR_IS_SET(CA_BYTESWAPPED);
	else
		swap_on_output = ATTR_IS_SET(CA_BYTESWAPPED);

	if (swap_on_output) {
		ENABLE_OPTION(chain_attrs, CA_BYTESWAP_ON_OUTPUT);
	}
}

/*
 * Given a desired payload size, determine whether we can read it in
 * immediately. If not, we have to wait for memory to become available.
 *
 * chain_read() is a serial chain step and will always be called by the same
 * thread. However, multiple other steps in the chain may want to modify or
 * free payloads, so memory tracking has to be managed with multithreading
 * in mind.
 *
 * The common case is that we are nowhere near the limit, which costs only a
 * single unsynchronized read of dif_current. We lock the mutex only when we
 * are actually going to await the condition.
 *
 * The store to dif_waiting and the subsequent load of dif_current are the
 * mirror image of the sequence in set_payload_impl(), which stores
 * dif_current and then loads dif_waiting. Both halves must be sequentially
 * consistent: if either were weaker, the two threads could miss each
 * other's store.
 */
static inline void
maybe_wait_for_memory(size_t bytes_wanted)
{
	uint64_t current = __atomic_load_n(&payloads.dif_current,
	    __ATOMIC_RELAXED);

	if (current + bytes_wanted <= payloads.dif_allowed)
		return;

	pthread_mutex_lock(&payloads.dif_mutex);
	__atomic_store_n(&payloads.dif_waiting, B_TRUE, __ATOMIC_SEQ_CST);
	while (__atomic_load_n(&payloads.dif_current, __ATOMIC_SEQ_CST) >
	    payloads.dif_resume) {
		pthread_cond_wait(&payloads.dif_cond, &payloads.dif_mutex);
	}
	/*
	 * A freeing thread that sees a stale B_TRUE here just takes the
	 * mutex for a broadcast that no one is waiting for, so this store
	 * needs no ordering of its own.
	 */
	__atomic_store_n(&payloads.dif_waiting, B_FALSE, __ATOMIC_RELAXED);
	pthread_mutex_unlock(&payloads.dif_mutex);
}

/*
 * Read in an item's payload. We don't do memory accounting here because
 * that's now handled by set_payload(). This function reads the payload into
 * a newly allocated buffer and returns the buffer. set_payload() attaches
 * an existing buffer to a dp_drr_t item.
 */
static inline uint8_t *
read_payload(io_context_t *context, size_t size)
{
	maybe_wait_for_memory(size);
	uint8_t *buff = safe_malloc(size);
	size_t n_read = fread(buff, size, 1, context->ic_fp);
	if (n_read != 1) {
		if (ferror(context->ic_fp)) {
			err(1, "error reading record payload at offset %llu",
			    (u_longlong_t)context->ic_offset);
		} else {
			/*
			 * We can't exit here because ZTS depends on being
			 * able to process randomly truncated streams.
			 */
			warnx("input ends mid-record at offset %llu - "
			    "stream is likely corrupt",
			    (u_longlong_t)context->ic_offset);
			fclose(context->ic_fp);
			free(buff);
			return (NULL);
		}
	}
	return (buff);
}

static disposition_t
chain_read(void *item_in, void *context_in)
{
	drr_packet_t *item = (drr_packet_t *)item_in;
	io_context_t *context = (io_context_t *)context_in;

	if (item == NULL)
		return (D_OK);

	dmu_replay_record_t *drr = &item->dp_drr;

	if (!context->ic_fp)
		open_file(context);

	item->dp_payload = NULL;
	item->dp_payload_size = 0;
	item->dp_stream_offset = context->ic_offset;

	if (fread(drr, sizeof (dmu_replay_record_t), 1, context->ic_fp) != 1) {
		if (ferror(context->ic_fp)) {
			err(1, "error reading record header at offset %llu",
			    (u_longlong_t)context->ic_offset);
		}
		fclose(context->ic_fp);
		return (D_EOF);
	}

	if (context->ic_offset == 0)
		set_stream_attributes(item);

	size_t payload_size = calc_payload_size(drr);
	if (payload_size > UINT32_MAX) {
		errx(1, "stated packet size is greater than uint32_t "
		    "at offset %llu", (u_longlong_t)context->ic_offset);
	} else if (payload_size > 0) {
		uint8_t *buff = read_payload(context, payload_size);
		if (buff == NULL)
			return (D_EOF);
		set_payload(item, buff, payload_size);
	}

	uint32_t drr_type = ATTR_IS_SET(CA_BYTESWAPPED) ?
	    BSWAP_32(drr->drr_type) : drr->drr_type;
	if (drr_type >= DRR_NUMTYPES) {
		err(1, "invalid record type %llu found at offset %llu",
		    (u_longlong_t)drr_type, (u_longlong_t)context->ic_offset);
	}

	context->ic_offset += sizeof (*drr) + item->dp_payload_size;

	record_stats_t *stats = &chain_attrs->ca_stats_in[drr_type];
	stats->rs_num_records++;
	stats->rs_total_header_bytes += sizeof (dmu_replay_record_t);
	stats->rs_total_payload_bytes += item->dp_payload_size;

	stats = &chain_attrs->ca_totals_in;
	stats->rs_num_records++;
	stats->rs_total_header_bytes += sizeof (dmu_replay_record_t);
	stats->rs_total_payload_bytes += item->dp_payload_size;

	return (D_OK);
}

static disposition_t
chain_write(void *item_in, void *context_in)
{
	drr_packet_t *item = (drr_packet_t *)item_in;
	io_context_t *context = (io_context_t *)context_in;

	if (item == NULL) {
		if (context->ic_fp) {
			if (fclose(context->ic_fp) != 0)
				err(1, "error closing output stream");
			context->ic_fp = NULL;
		}
		VERIFY0(__atomic_load_n(&payloads.dif_current,
		    __ATOMIC_SEQ_CST));
		return (D_OK);
	}

	if (!context->ic_fp) {
		open_file(context);
	}

	dmu_replay_record_t *drr = &item->dp_drr;

	if (fwrite(drr, sizeof (dmu_replay_record_t), 1, context->ic_fp) != 1) {
		err(1, "error writing record header");
	} else if (item->dp_payload_size > 0) {
		size_t n_written = fwrite(item->dp_payload,
		    item->dp_payload_size, 1, context->ic_fp);
		if (n_written != 1) {
			err(1, "error writing payload");
		}
	}

	uint32_t drr_type = OPTION_ENABLED(CA_BYTESWAP_ON_OUTPUT) ?
	    BSWAP_32(drr->drr_type) : drr->drr_type;

	record_stats_t *stats = &chain_attrs->ca_stats_out[drr_type];
	stats->rs_num_records++;
	stats->rs_total_header_bytes += sizeof (dmu_replay_record_t);
	stats->rs_total_payload_bytes += item->dp_payload_size;

	stats = &chain_attrs->ca_totals_out;
	stats->rs_num_records++;
	stats->rs_total_header_bytes += sizeof (dmu_replay_record_t);
	stats->rs_total_payload_bytes += item->dp_payload_size;

	set_payload(item, NULL, 0);
	return (D_OK);
}

/*
 * Even if the chain doesn't write out a stream, payloads still need freed.
 */
static disposition_t
chain_null_output(void *item_in, void *context)
{
	(void) context;
	drr_packet_t *item = (drr_packet_t *)item_in;

	if (item == NULL)
		return (D_OK);

	set_payload(item, NULL, 0);
	return (D_OK);
}

/*
 * Storage for the filename must remain valid during chain execution
 */
static chain_step_t
setup_io(const char *filename, boolean_t for_reading)
{
	pthread_once(&dif_init_control, initialize_memory_tracking);
	int context_num = next_io_context++ % MAX_IO_STREAMS;

	io_context_t context = {
		.ic_filename = filename,
		.ic_for_reading = for_reading
	};
	io_contexts[context_num] = context;

	chain_step_t step = {
		.cs_type = CS_SERIAL,
		.cs_in_size = for_reading ? 0 : sizeof (drr_packet_t),
		.cs_out_size = for_reading ? sizeof (drr_packet_t) : 0,
		.cs_context = &io_contexts[context_num],
		.cs_serial = {
			.process = for_reading ? chain_read : chain_write
		}
	};
	return (step);
}

chain_step_t
serial_read_stream(const char *filename)
{
	return (setup_io(filename, B_TRUE));
}

chain_step_t
serial_write_stream(const char *filename)
{
	return (setup_io(filename, B_FALSE));
}

chain_step_t
serial_null_output(void)
{
	chain_step_t step = {
		.cs_type = CS_SERIAL,
		.cs_in_size = sizeof (drr_packet_t),
		.cs_out_size = 0,
		.cs_context = NULL,
		.cs_serial = {
			.process = chain_null_output
		}
	};
	return (step);
}

size_t
constant_cost_of_one(queue_item_t *packet, void *context)
{
	(void) context;
	(void) packet;
	return (1);
}

size_t
payload_size_as_cost(queue_item_t *packet_in, void *context)
{
	(void) context;
	drr_packet_t *packet = (drr_packet_t *)packet_in;
	return (packet->dp_payload_size);
}

static disposition_t
chain_checkpoint(void *item_in, void *ctxt_in)
{
	drr_packet_t *item = (drr_packet_t *)item_in;
	checkpoint_context_t *ctxt = (checkpoint_context_t *)ctxt_in;

	struct timespec now;
	char buff[32];
	uint64_t delta_b, dbdt;
	double now_sec, delta_t;

	if (item == NULL)
		return (D_OK);

	clock_gettime(CLOCK_MONOTONIC, &now);
	now_sec = now.tv_sec + (double)now.tv_nsec / 1E9;
	if (ctxt->cc_last_sec > 1E-9) {
		delta_t = now_sec - ctxt->cc_last_sec;
		if (delta_t < ctxt->cc_period_sec)
			return (D_OK);
		delta_b = item->dp_stream_offset - ctxt->cc_last_bytes;
		dbdt = delta_b / delta_t;
		zfs_nicenum(dbdt, buff, sizeof (buff));
		fprintf(stderr, "Checkpoint %s: %s/s\n", ctxt->cc_name, buff);
	}
	ctxt->cc_last_sec = now_sec;
	ctxt->cc_last_bytes = item->dp_stream_offset;
	return (D_OK);
}

/*
 * Storage for name must remain valid throughout chain execution
 */
chain_step_t
serial_checkpoint(const char *name)
{
	int context_no = next_checkpoint_context++ % MAX_IO_STREAMS;

	checkpoint_context_t context = {
		.cc_name = name,
		.cc_period_sec = 1.0
	};
	checkpoint_contexts[context_no] = context;

	chain_step_t step = {
		.cs_type = CS_SERIAL,
		.cs_in_size = sizeof (drr_packet_t),
		.cs_out_size = sizeof (drr_packet_t),
		.cs_context = &checkpoint_contexts[context_no],
		.cs_serial = {
			.process = chain_checkpoint
		},
	};
	return (step);
}

static disposition_t
chain_drop_record_types(void *item_in, void *context_in)
{
	drr_packet_t *item = (drr_packet_t *)item_in;
	uint32_t *context = (uint32_t *)context_in;

	if (item == NULL)
		return (D_OK);

	uint32_t type = (uint32_t)item->dp_drr.drr_type;
	if (type >= DRR_NUMTYPES) {
		errx(1, "invalid record type %u found at offset %llu "
		    "(place drop filter downstream of byteswapping?)",
		    type, (u_longlong_t)item->dp_stream_offset);
	}

	if (((UINT32_C(1) << type) & *context) != 0) {
		set_payload(item, NULL, 0);
		return (D_DROP);
	}
	return (D_OK);
}

chain_step_t
serial_drop_record_types(uint32_t drop_mask)
{
	int context_no = next_drop_context++ % MAX_DROP_FILTERS;
	uint32_t *context = &drop_contexts[context_no];

	*context = drop_mask;

	chain_step_t step = {
		.cs_type = CS_SERIAL,
		.cs_in_size = sizeof (drr_packet_t),
		.cs_out_size = sizeof (drr_packet_t),
		.cs_context = context,
		.cs_serial = {
			.process = chain_drop_record_types
		},
	};
	return (step);
}

/*
 * Every record that carries a payload passes through here at least twice,
 * once on the way in and once on the way out. Those calls are also made by
 * different threads, which unfortunately is something of an adversarial
 * pattern for a pthreads mutex. The cache line containing the lock
 * structure ping-pongs among cores, and each move is performed with
 * restrictive memory barriers. We use atomic operations instead and reserve
 * the mutex for waking up a sleeping memory consumer.
 *
 * The atomic update is done in sequentially consistent mode because the
 * (potential) load of dif_waiting beneath must not be hoisted above the
 * update to dif_current. dif_waiting shares a cache line with dif_current,
 * which we have just acquired exclusively, so reading it is essentially
 * free.
 */
static void
set_payload_impl(void *item_in, void *payload_in, uint64_t size,
    boolean_t free_old)
{
	drr_packet_t *item = (drr_packet_t *)item_in;
	uint8_t *payload = (uint8_t *)payload_in;
	VERIFY(payload != NULL || size == 0);
	VERIFY(payload == NULL || payload != item->dp_payload);

	if (free_old && item->dp_payload != NULL) {
		free(item->dp_payload);
	}
	int64_t delta = (int64_t)size - (int64_t)item->dp_payload_size;
	VERIFY3U(size, <=, UINT32_MAX);
	item->dp_payload = payload;
	item->dp_payload_size = (uint32_t)size;

	/*
	 * Atomics are nominally unsigned operations, but because of twos
	 * complement arithmetic it's fine to add a "negative" value.
	 */
	uint64_t current = __atomic_add_fetch(&payloads.dif_current, delta,
	    __ATOMIC_SEQ_CST);

	/*
	 * Only a net reduction can release a parked reader, and only if it
	 * brings us back under the hysteresis threshold.
	 */
	if (delta < 0 && current <= payloads.dif_resume &&
	    __atomic_load_n(&payloads.dif_waiting, __ATOMIC_SEQ_CST)) {
		pthread_mutex_lock(&payloads.dif_mutex);
		pthread_cond_broadcast(&payloads.dif_cond);
		pthread_mutex_unlock(&payloads.dif_mutex);
	}
}

void
set_payload(void *item_in, void *payload_in, uint64_t size)
{
	set_payload_impl(item_in, payload_in, size, B_TRUE);
}

/*
 * Remove a payload from the chain's management without freeing. It's up to
 * the recipient to free the buffer.
 */
void
export_payload(void *item_in)
{
	set_payload_impl(item_in, NULL, 0, B_FALSE);
}
