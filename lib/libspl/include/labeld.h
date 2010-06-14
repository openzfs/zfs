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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_LABELD_H
#define	_LABELD_H

#include <sys/types.h>
#include <tsol/label.h>
#include <sys/tsol/label_macro.h>

/*
 *	Definitions for the call parameters for the door-based label
 * translation service.
 */

#define	BUFSIZE	4096

#define	DOOR_PATH	"/var/tsol/doors/"
#define	DOOR_NAME	"labeld"
#define	COOKIE		(void *)0x6c616264	/* "labd" */

/*	Op codes */

/*	Labeld Commands */

#define	LABELDNULL	1

/*	Miscellaneous */

#define	BLINSET		10
#define	BSLVALID	11
#define	BILVALID	12
#define	BCLEARVALID	13
#define	LABELINFO	14
#define	LABELVERS	15
#define	BLTOCOLOR	16

/*	Binary to String Label Translation */

#define	BSLTOS		23
#define	BCLEARTOS	25

/*	String to Binary Label Translation */

#define	STOBSL		31
#define	STOBCLEAR	33

/*
 *	Dimming List Routines
 *	Contract private for label builders
 */

#define	BSLCVT		40
#define	BCLEARCVT	42
#define	LABELFIELDS	43
#define	UDEFS		44

#define	GETFLABEL	45
#define	SETFLABEL	46
#define	ZCOPY		47

/* NEW LABELS */
/* DIA printer banner labels */

#define	PR_CAVEATS	101
#define	PR_CHANNELS	102
#define	PR_LABEL	103
#define	PR_TOP		104

/* DIA label to string  */

#define	LTOS		105

/* DIA string to label */

#define	STOL		106

/*	Structures */

typedef	uint_t	bufp_t;		/* offset into buf[] in/out string buffer */

/* Null call */

typedef	struct {
	int	null;
} null_call_t;

typedef	struct {
	int	null;
} null_ret_t;

/* Miscellaneous interfaces */

typedef	struct {
	bslabel_t label;
	int	type;
} inset_call_t;

typedef	struct {
	int	inset;
} inset_ret_t;

typedef	struct {
	bslabel_t label;
} slvalid_call_t;

typedef	struct {
	int	valid;
} slvalid_ret_t;

typedef	struct {
	bclear_t clear;
} clrvalid_call_t;

typedef	struct {
	int	valid;
} clrvalid_ret_t;

typedef	struct {
	int	null;
} info_call_t;

typedef	struct {
	struct label_info info;
} info_ret_t;

typedef	struct {
	int	null;
} vers_call_t;

typedef	struct {
	char	vers[BUFSIZE];
} vers_ret_t;

typedef struct {
	blevel_t label;
} color_call_t;

typedef struct {
	char	color[BUFSIZE];
} color_ret_t;

/* Binary Label to String interfaces */

typedef	struct {
	bslabel_t label;
	uint_t	flags;
} bsltos_call_t;

typedef	struct {
	char	slabel[BUFSIZE];
} bsltos_ret_t;

typedef	struct {
	bclear_t clear;
	uint_t	flags;
} bcleartos_call_t;

typedef	struct {
	char	cslabel[BUFSIZE];
} bcleartos_ret_t;

/* String to Binary Label interfaces */

typedef	struct {
	bslabel_t label;
	uint_t	flags;
	char	string[BUFSIZE];
} stobsl_call_t;

typedef	struct {
	bslabel_t label;
} stobsl_ret_t;

typedef	struct {
	bclear_t clear;
	uint_t	flags;
	char	string[BUFSIZE];
} stobclear_call_t;

typedef	struct {
	bclear_t clear;
} stobclear_ret_t;

/*
 * The following Dimming List and Miscellaneous interfaces
 * implement contract private interfaces for the label builder
 * interfaces.
 */

/* Dimming List interfaces */

typedef	struct {
	bslabel_t label;
	brange_t bounds;
	uint_t	flags;
} bslcvt_call_t;

typedef	struct {
	bufp_t	string;
	bufp_t	dim;
	bufp_t	lwords;
	bufp_t	swords;
	size_t	d_len;
	size_t	l_len;
	size_t	s_len;
	int	first_comp;
	int	first_mark;
	char	buf[BUFSIZE];
} cvt_ret_t;

typedef cvt_ret_t bslcvt_ret_t;

typedef	struct {
	bclear_t clear;
	brange_t bounds;
	uint_t	flags;
} bclearcvt_call_t;

typedef cvt_ret_t bclearcvt_ret_t;

/* Miscellaneous interfaces */

typedef	struct {
	int	null;
} fields_call_t;

typedef	struct {
	bufp_t	classi;
	bufp_t	compsi;
	bufp_t	marksi;
	char	buf[BUFSIZE];
} fields_ret_t;

typedef	struct {
	int	null;
} udefs_call_t;

typedef	struct {
	bslabel_t sl;
	bclear_t  clear;
} udefs_ret_t;

typedef	struct {
	bslabel_t  sl;
	char	pathname[BUFSIZE];
} setfbcl_call_t;

typedef	struct {
	int	status;
} setfbcl_ret_t;

typedef	struct {
	bslabel_t  src_win_sl;
	int	transfer_mode;
	bufp_t  remote_dir;
	bufp_t  filename;
	bufp_t  local_dir;
	bufp_t  display;
	char    buf[BUFSIZE];
} zcopy_call_t;

typedef	struct {
	int	status;
} zcopy_ret_t;

typedef	struct {
	m_label_t label;
	uint_t	flags;
} pr_call_t;

typedef	struct {
	char	buf[BUFSIZE];
} pr_ret_t;

typedef	struct {
	m_label_t label;
	uint_t	flags;
} ls_call_t;

typedef	struct {
	char	buf[BUFSIZE];
} ls_ret_t;

typedef	struct {
	m_label_t label;
	uint_t	flags;
	char	string[BUFSIZE];
} sl_call_t;

typedef	struct {
	m_label_t label;
} sl_ret_t;

/* Labeld operation call structure */

typedef	struct {
	uint_t	op;
	union	{
		null_call_t	null_arg;

		inset_call_t	inset_arg;
		slvalid_call_t	slvalid_arg;
		clrvalid_call_t	clrvalid_arg;
		info_call_t	info_arg;
		vers_call_t	vers_arg;
		color_call_t	color_arg;

		bsltos_call_t	bsltos_arg;
		bcleartos_call_t	bcleartos_arg;

		stobsl_call_t	stobsl_arg;
		stobclear_call_t	stobclear_arg;

		bslcvt_call_t	bslcvt_arg;
		bclearcvt_call_t	bclearcvt_arg;
		fields_call_t	fields_arg;
		udefs_call_t	udefs_arg;
		setfbcl_call_t	setfbcl_arg;
		zcopy_call_t	zcopy_arg;
		pr_call_t	pr_arg;
		ls_call_t	ls_arg;
		sl_call_t	sl_arg;
	} cargs;
} labeld_call_t;

/* Labeld operation return structure */

typedef struct {
	int	ret;		/* labeld return codes */
	int	err;		/* function error codes */
	union	{
		null_ret_t	null_ret;

		inset_ret_t	inset_ret;
		slvalid_ret_t	slvalid_ret;
		clrvalid_ret_t	clrvalid_ret;
		info_ret_t	info_ret;
		vers_ret_t	vers_ret;
		color_ret_t	color_ret;

		bsltos_ret_t	bsltos_ret;
		bcleartos_ret_t	bcleartos_ret;

		stobsl_ret_t	stobsl_ret;
		stobclear_ret_t	stobclear_ret;

		bslcvt_ret_t	bslcvt_ret;
		bclearcvt_ret_t	bclearcvt_ret;
		fields_ret_t	fields_ret;
		udefs_ret_t	udefs_ret;
		setfbcl_ret_t	setfbcl_ret;
		zcopy_ret_t	zcopy_ret;
		pr_ret_t	pr_ret;
		ls_ret_t	ls_ret;
		sl_ret_t	sl_ret;
	} rvals;
} labeld_ret_t;

/* Labeld call/return structure */

typedef	struct {
	union {
		labeld_call_t	acall;
		labeld_ret_t	aret;
	} param;
} labeld_data_t;

#define	callop	param.acall.op
#define	retret	param.aret.ret
#define	reterr	param.aret.err

#define	CALL_SIZE(type, buf)	(size_t)(sizeof (type) + sizeof (int) + (buf))
#define	RET_SIZE(type, buf)	(size_t)(sizeof (type) + 2*sizeof (int) + (buf))
#define	CALL_SIZE_STR(type, buf)	CALL_SIZE(type, (-BUFSIZE +(buf)))

/* Return Codes */

#define	SUCCESS		1	/* Call OK */
#define	NOTFOUND	-1	/* Function not found */
#define	SERVERFAULT	-2	/* Internal labeld error */
#define	NOSERVER	-3	/* No server thread available, try later */

/* Labeld common client call function */

static inline int
__call_labeld(labeld_data_t **dptr, size_t *ndata, size_t *adata)
{
	return NOSERVER;
}

/* Flag Translation Values */

#define	L_NEW_LABEL		0x10000000

/* GFI FLAGS */

#define	GFI_FLAG_MASK		 0x0000FFFF
#define	GFI_ACCESS_RELATED	 0x00000001

/* binary to ASCII */

#define	LABELS_NO_CLASS		 0x00010000
#define	LABELS_SHORT_CLASS	 0x00020000
#define	LABELS_SHORT_WORDS	 0x00040000

/* Label view */

#define	LABELS_VIEW_INTERNAL	 0x00100000
#define	LABELS_VIEW_EXTERNAL	 0x00200000

/* Dimming list (convert -- b*cvt* ) */

#define	LABELS_FULL_CONVERT	 0x00010000

/* ASCII to binary */

#define	LABELS_NEW_LABEL	 0x00010000
#define	LABELS_FULL_PARSE	 0x00020000
#define	LABELS_ONLY_INFO_LABEL	 0x00040000

#define	MOVE_FILE	0
#define	COPY_FILE	1
#define	LINK_FILE	2

#define	PIPEMSG_FILEOP_ERROR	1
#define	PIPEMSG_EXIST_ERROR	2
#define	PIPEMSG_DONE 		7
#define	PIPEMSG_PATH_ERROR	20
#define	PIPEMSG_ZONE_ERROR	21
#define	PIPEMSG_LABEL_ERROR	22
#define	PIPEMSG_READ_ERROR	23
#define	PIPEMSG_READONLY_ERROR  24
#define	PIPEMSG_WRITE_ERROR	25
#define	PIPEMSG_CREATE_ERROR	26
#define	PIPEMSG_DELETE_ERROR	27
#define	PIPEMSG_CANCEL		101
#define	PIPEMSG_PROCEED		102
#define	PIPEMSG_MERGE		103
#define	PIPEMSG_REPLACE_BUFFER	104
#define	PIPEMSG_RENAME_BUFFER	105
#define	PIPEMSG_MULTI_PROCEED	106
#define	PIPEMSG_RENAME_FILE	107

#endif	/* _LABELD_H */
