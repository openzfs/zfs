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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Print intent log header and statistics.
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/dmu.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/zil.h>
#include <sys/zil_impl.h>

extern uint8_t dump_opt[256];

static char prefix[4] = "\t\t\t";

static void
print_log_bp(const blkptr_t *bp, const char *prefix)
{
	char blkbuf[BP_SPRINTF_LEN];

	sprintf_blkptr(blkbuf, bp);
	(void) printf("%s%s\n", prefix, blkbuf);
}

/* ARGSUSED */
static void
zil_prt_rec_create(zilog_t *zilog, int txtype, lr_create_t *lr)
{
	time_t crtime = lr->lr_crtime[0];
	char *name, *link;
	lr_attr_t *lrattr;

	name = (char *)(lr + 1);

	if (lr->lr_common.lrc_txtype == TX_CREATE_ATTR ||
	    lr->lr_common.lrc_txtype == TX_MKDIR_ATTR) {
		lrattr = (lr_attr_t *)(lr + 1);
		name += ZIL_XVAT_SIZE(lrattr->lr_attr_masksize);
	}

	if (txtype == TX_SYMLINK) {
		link = name + strlen(name) + 1;
		(void) printf("%s%s -> %s\n", prefix, name, link);
	} else if (txtype != TX_MKXATTR) {
		(void) printf("%s%s\n", prefix, name);
	}

	(void) printf("%s%s", prefix, ctime(&crtime));
	(void) printf("%sdoid %llu, foid %llu, mode %llo\n", prefix,
	    (u_longlong_t)lr->lr_doid, (u_longlong_t)lr->lr_foid,
	    (longlong_t)lr->lr_mode);
	(void) printf("%suid %llu, gid %llu, gen %llu, rdev 0x%llx\n", prefix,
	    (u_longlong_t)lr->lr_uid, (u_longlong_t)lr->lr_gid,
	    (u_longlong_t)lr->lr_gen, (u_longlong_t)lr->lr_rdev);
}

/* ARGSUSED */
static void
zil_prt_rec_remove(zilog_t *zilog, int txtype, lr_remove_t *lr)
{
	(void) printf("%sdoid %llu, name %s\n", prefix,
	    (u_longlong_t)lr->lr_doid, (char *)(lr + 1));
}

/* ARGSUSED */
static void
zil_prt_rec_link(zilog_t *zilog, int txtype, lr_link_t *lr)
{
	(void) printf("%sdoid %llu, link_obj %llu, name %s\n", prefix,
	    (u_longlong_t)lr->lr_doid, (u_longlong_t)lr->lr_link_obj,
	    (char *)(lr + 1));
}

/* ARGSUSED */
static void
zil_prt_rec_rename(zilog_t *zilog, int txtype, lr_rename_t *lr)
{
	char *snm = (char *)(lr + 1);
	char *tnm = snm + strlen(snm) + 1;

	(void) printf("%ssdoid %llu, tdoid %llu\n", prefix,
	    (u_longlong_t)lr->lr_sdoid, (u_longlong_t)lr->lr_tdoid);
	(void) printf("%ssrc %s tgt %s\n", prefix, snm, tnm);
}

/* ARGSUSED */
static void
zil_prt_rec_write(zilog_t *zilog, int txtype, lr_write_t *lr)
{
	char *data, *dlimit;
	blkptr_t *bp = &lr->lr_blkptr;
	zbookmark_t zb;
	char buf[SPA_MAXBLOCKSIZE];
	int verbose = MAX(dump_opt['d'], dump_opt['i']);
	int error;

	(void) printf("%sfoid %llu, offset %llx, length %llx\n", prefix,
	    (u_longlong_t)lr->lr_foid, (u_longlong_t)lr->lr_offset,
	    (u_longlong_t)lr->lr_length);

	if (txtype == TX_WRITE2 || verbose < 5)
		return;

	if (lr->lr_common.lrc_reclen == sizeof (lr_write_t)) {
		(void) printf("%shas blkptr, %s\n", prefix,
		    bp->blk_birth >= spa_first_txg(zilog->zl_spa) ?
		    "will claim" : "won't claim");
		print_log_bp(bp, prefix);

		if (BP_IS_HOLE(bp)) {
			(void) printf("\t\t\tLSIZE 0x%llx\n",
			    (u_longlong_t)BP_GET_LSIZE(bp));
		}
		if (bp->blk_birth == 0) {
			bzero(buf, sizeof (buf));
			(void) printf("%s<hole>\n", prefix);
			return;
		}
		if (bp->blk_birth < zilog->zl_header->zh_claim_txg) {
			(void) printf("%s<block already committed>\n", prefix);
			return;
		}

		SET_BOOKMARK(&zb, dmu_objset_id(zilog->zl_os),
		    lr->lr_foid, ZB_ZIL_LEVEL,
		    lr->lr_offset / BP_GET_LSIZE(bp));

		error = zio_wait(zio_read(NULL, zilog->zl_spa,
		    bp, buf, BP_GET_LSIZE(bp), NULL, NULL,
		    ZIO_PRIORITY_SYNC_READ, ZIO_FLAG_CANFAIL, &zb));
		if (error)
			return;
		data = buf;
	} else {
		data = (char *)(lr + 1);
	}

	dlimit = data + MIN(lr->lr_length,
	    (verbose < 6 ? 20 : SPA_MAXBLOCKSIZE));

	(void) printf("%s", prefix);
	while (data < dlimit) {
		if (isprint(*data))
			(void) printf("%c ", *data);
		else
			(void) printf("%2X", *data);
		data++;
	}
	(void) printf("\n");
}

/* ARGSUSED */
static void
zil_prt_rec_truncate(zilog_t *zilog, int txtype, lr_truncate_t *lr)
{
	(void) printf("%sfoid %llu, offset 0x%llx, length 0x%llx\n", prefix,
	    (u_longlong_t)lr->lr_foid, (longlong_t)lr->lr_offset,
	    (u_longlong_t)lr->lr_length);
}

/* ARGSUSED */
static void
zil_prt_rec_setattr(zilog_t *zilog, int txtype, lr_setattr_t *lr)
{
	time_t atime = (time_t)lr->lr_atime[0];
	time_t mtime = (time_t)lr->lr_mtime[0];

	(void) printf("%sfoid %llu, mask 0x%llx\n", prefix,
	    (u_longlong_t)lr->lr_foid, (u_longlong_t)lr->lr_mask);

	if (lr->lr_mask & AT_MODE) {
		(void) printf("%sAT_MODE  %llo\n", prefix,
		    (longlong_t)lr->lr_mode);
	}

	if (lr->lr_mask & AT_UID) {
		(void) printf("%sAT_UID   %llu\n", prefix,
		    (u_longlong_t)lr->lr_uid);
	}

	if (lr->lr_mask & AT_GID) {
		(void) printf("%sAT_GID   %llu\n", prefix,
		    (u_longlong_t)lr->lr_gid);
	}

	if (lr->lr_mask & AT_SIZE) {
		(void) printf("%sAT_SIZE  %llu\n", prefix,
		    (u_longlong_t)lr->lr_size);
	}

	if (lr->lr_mask & AT_ATIME) {
		(void) printf("%sAT_ATIME %llu.%09llu %s", prefix,
		    (u_longlong_t)lr->lr_atime[0],
		    (u_longlong_t)lr->lr_atime[1],
		    ctime(&atime));
	}

	if (lr->lr_mask & AT_MTIME) {
		(void) printf("%sAT_MTIME %llu.%09llu %s", prefix,
		    (u_longlong_t)lr->lr_mtime[0],
		    (u_longlong_t)lr->lr_mtime[1],
		    ctime(&mtime));
	}
}

/* ARGSUSED */
static void
zil_prt_rec_acl(zilog_t *zilog, int txtype, lr_acl_t *lr)
{
	(void) printf("%sfoid %llu, aclcnt %llu\n", prefix,
	    (u_longlong_t)lr->lr_foid, (u_longlong_t)lr->lr_aclcnt);
}

typedef void (*zil_prt_rec_func_t)();
typedef struct zil_rec_info {
	zil_prt_rec_func_t	zri_print;
	char			*zri_name;
	uint64_t		zri_count;
} zil_rec_info_t;

static zil_rec_info_t zil_rec_info[TX_MAX_TYPE] = {
	{	NULL,			"Total              " },
	{	zil_prt_rec_create,	"TX_CREATE          " },
	{	zil_prt_rec_create,	"TX_MKDIR           " },
	{	zil_prt_rec_create,	"TX_MKXATTR         " },
	{	zil_prt_rec_create,	"TX_SYMLINK         " },
	{	zil_prt_rec_remove,	"TX_REMOVE          " },
	{	zil_prt_rec_remove,	"TX_RMDIR           " },
	{	zil_prt_rec_link,	"TX_LINK            " },
	{	zil_prt_rec_rename,	"TX_RENAME          " },
	{	zil_prt_rec_write,	"TX_WRITE           " },
	{	zil_prt_rec_truncate,	"TX_TRUNCATE        " },
	{	zil_prt_rec_setattr,	"TX_SETATTR         " },
	{	zil_prt_rec_acl,	"TX_ACL_V0          " },
	{	zil_prt_rec_acl,	"TX_ACL_ACL         " },
	{	zil_prt_rec_create,	"TX_CREATE_ACL      " },
	{	zil_prt_rec_create,	"TX_CREATE_ATTR     " },
	{	zil_prt_rec_create,	"TX_CREATE_ACL_ATTR " },
	{	zil_prt_rec_create,	"TX_MKDIR_ACL       " },
	{	zil_prt_rec_create,	"TX_MKDIR_ATTR      " },
	{	zil_prt_rec_create,	"TX_MKDIR_ACL_ATTR  " },
	{	zil_prt_rec_write,	"TX_WRITE2          " },
};

/* ARGSUSED */
static int
print_log_record(zilog_t *zilog, lr_t *lr, void *arg, uint64_t claim_txg)
{
	int txtype;
	int verbose = MAX(dump_opt['d'], dump_opt['i']);

	/* reduce size of txtype to strip off TX_CI bit */
	txtype = lr->lrc_txtype;

	ASSERT(txtype != 0 && (uint_t)txtype < TX_MAX_TYPE);
	ASSERT(lr->lrc_txg);

	(void) printf("\t\t%s%s len %6llu, txg %llu, seq %llu\n",
	    (lr->lrc_txtype & TX_CI) ? "CI-" : "",
	    zil_rec_info[txtype].zri_name,
	    (u_longlong_t)lr->lrc_reclen,
	    (u_longlong_t)lr->lrc_txg,
	    (u_longlong_t)lr->lrc_seq);

	if (txtype && verbose >= 3)
		zil_rec_info[txtype].zri_print(zilog, txtype, lr);

	zil_rec_info[txtype].zri_count++;
	zil_rec_info[0].zri_count++;

	return (0);
}

/* ARGSUSED */
static int
print_log_block(zilog_t *zilog, blkptr_t *bp, void *arg, uint64_t claim_txg)
{
	char blkbuf[BP_SPRINTF_LEN + 10];
	int verbose = MAX(dump_opt['d'], dump_opt['i']);
	char *claim;

	if (verbose <= 3)
		return (0);

	if (verbose >= 5) {
		(void) strcpy(blkbuf, ", ");
		sprintf_blkptr(blkbuf + strlen(blkbuf), bp);
	} else {
		blkbuf[0] = '\0';
	}

	if (claim_txg != 0)
		claim = "already claimed";
	else if (bp->blk_birth >= spa_first_txg(zilog->zl_spa))
		claim = "will claim";
	else
		claim = "won't claim";

	(void) printf("\tBlock seqno %llu, %s%s\n",
	    (u_longlong_t)bp->blk_cksum.zc_word[ZIL_ZC_SEQ], claim, blkbuf);

	return (0);
}

static void
print_log_stats(int verbose)
{
	int i, w, p10;

	if (verbose > 3)
		(void) printf("\n");

	if (zil_rec_info[0].zri_count == 0)
		return;

	for (w = 1, p10 = 10; zil_rec_info[0].zri_count >= p10; p10 *= 10)
		w++;

	for (i = 0; i < TX_MAX_TYPE; i++)
		if (zil_rec_info[i].zri_count || verbose >= 3)
			(void) printf("\t\t%s %*llu\n",
			    zil_rec_info[i].zri_name, w,
			    (u_longlong_t)zil_rec_info[i].zri_count);
	(void) printf("\n");
}

/* ARGSUSED */
void
dump_intent_log(zilog_t *zilog)
{
	const zil_header_t *zh = zilog->zl_header;
	int verbose = MAX(dump_opt['d'], dump_opt['i']);
	int i;

	if (zh->zh_log.blk_birth == 0 || verbose < 1)
		return;

	(void) printf("\n    ZIL header: claim_txg %llu, "
	    "claim_blk_seq %llu, claim_lr_seq %llu",
	    (u_longlong_t)zh->zh_claim_txg,
	    (u_longlong_t)zh->zh_claim_blk_seq,
	    (u_longlong_t)zh->zh_claim_lr_seq);
	(void) printf(" replay_seq %llu, flags 0x%llx\n",
	    (u_longlong_t)zh->zh_replay_seq, (u_longlong_t)zh->zh_flags);

	for (i = 0; i < TX_MAX_TYPE; i++)
		zil_rec_info[i].zri_count = 0;

	if (verbose >= 2) {
		(void) printf("\n");
		(void) zil_parse(zilog, print_log_block, print_log_record, NULL,
		    zh->zh_claim_txg);
		print_log_stats(verbose);
	}
}
