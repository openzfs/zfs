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

#ifndef	_TSOL_LABEL_H
#define	_TSOL_LABEL_H

#include <sys/types32.h>
#include <sys/tsol/label.h>
#include <priv.h>

/* Procedural Interface Structure Definitions */

struct	label_info {		/* structure returned by label_info */
	short	ilabel_len;		/* max Information Label length */
	short	slabel_len;		/* max Sensitivity Label length */
	short	clabel_len;		/* max CMW Label length */
	short	clear_len;		/* max Clearance Label length */
	short	vers_len;		/* version string length */
	short	header_len;		/* max len of banner page header */
	short	protect_as_len;		/* max len of banner page protect as */
	short	caveats_len;		/* max len of banner page caveats */
	short	channels_len;		/* max len of banner page channels */
};

typedef struct label_set_identifier {	/* valid label set identifier */
	int	type;			/* type of the set */
	char	*name;			/* name of the set if needed */
} set_id;

struct name_fields {		/* names for label builder fields */
	char	*class_name;		/* Classifications field name */
	char	*comps_name;		/* Compartments field name */
	char	*marks_name;		/* Markings field name */
};

/* Label Set Identifier Types */

/*
 * The accreditation ranges as specified in the label encodings file.
 * The name parameter is ignored.
 *
 * System Accreditation Range is all valid labels plus Admin High and Low.
 *
 * User Accreditation Range is valid user labels as defined in the
 *	ACCREDITATION RANGE: section of the label encodings file.
 */

#define	SYSTEM_ACCREDITATION_RANGE	1
#define	USER_ACCREDITATION_RANGE	2


/* System Call Interface Definitions */

extern int getlabel(const char *, m_label_t *);
extern int fgetlabel(int, m_label_t *);

extern int getplabel(m_label_t *);
extern int setflabel(const char *, m_label_t *);
extern char *getpathbylabel(const char *, char *, size_t,
    const m_label_t *sl);
extern m_label_t *getzonelabelbyid(zoneid_t);
extern m_label_t *getzonelabelbyname(const char *);
extern zoneid_t getzoneidbylabel(const m_label_t *);
extern char *getzonenamebylabel(const m_label_t *);
extern char *getzonerootbyid(zoneid_t);
extern char *getzonerootbyname(const char *);
extern char *getzonerootbylabel(const m_label_t *);
extern m_label_t *getlabelbypath(const char *);


/* Flag word values */

#define	ALL_ENTRIES		0x00000000
#define	ACCESS_RELATED		0x00000001
#define	ACCESS_MASK		0x0000FFFF
#define	ACCESS_SHIFT		0

#define	LONG_WORDS		0x00010000	/* use long names */
#define	SHORT_WORDS		0x00020000	/* use short names if present */
#define	LONG_CLASSIFICATION	0x00040000	/* use long classification */
#define	SHORT_CLASSIFICATION	0x00080000	/* use short classification */
#define	NO_CLASSIFICATION	0x00100000	/* don't translate the class */
#define	VIEW_INTERNAL		0x00200000	/* don't promote/demote */
#define	VIEW_EXTERNAL		0x00400000	/* promote/demote label */

#define	NEW_LABEL		0x00000001	/* create a full new label */
#define	NO_CORRECTION		0x00000002	/* don't correct label errors */
						/* implies NEW_LABEL */

#define	CVT_DIM			0x01		/* display word dimmed */
#define	CVT_SET			0x02		/* display word currently set */

/* Procedure Interface Definitions available to user */

/* APIs shared with the kernel are in <sys/tsol/label.h */

extern m_label_t *blabel_alloc(void);
extern void	blabel_free(m_label_t *);
extern size32_t blabel_size(void);
extern char	*bsltoh(const m_label_t *);
extern char	*bcleartoh(const m_label_t *);

extern char	*bsltoh_r(const m_label_t *, char *);
extern char	*bcleartoh_r(const m_label_t *, char *);
extern char	*h_alloc(uint8_t);
extern void	h_free(char *);

extern int	htobsl(const char *, m_label_t *);
extern int	htobclear(const char *, m_label_t *);

extern m_range_t	*getuserrange(const char *);
extern m_range_t	*getdevicerange(const char *);

extern int	set_effective_priv(priv_op_t, int, ...);
extern int	set_inheritable_priv(priv_op_t, int, ...);
extern int	set_permitted_priv(priv_op_t, int, ...);
extern int	is_system_labeled(void);

/* Procedures needed for multi-level printing */

extern int	tsol_check_admin_auth(uid_t uid);

/* APIs implemented via labeld */

extern int	blinset(const m_label_t *, const set_id *);
extern int	labelinfo(struct label_info *);
extern ssize_t	labelvers(char **, size_t);
extern char	*bltocolor(const m_label_t *);
extern char	*bltocolor_r(const m_label_t *, size_t, char *);

extern ssize_t	bsltos(const m_label_t *, char **, size_t, int);
extern ssize_t	bcleartos(const m_label_t *, char **, size_t, int);


extern char	*sbsltos(const m_label_t *, size_t);
extern char	*sbcleartos(const m_label_t *, size_t);


extern int	stobsl(const char *, m_label_t *, int, int *);
extern int	stobclear(const char *, m_label_t *, int, int *);
extern int	bslvalid(const m_label_t *);
extern int	bclearvalid(const m_label_t *);

/* DIA label conversion and parsing */

/* Conversion types */

typedef	enum _m_label_str {
	M_LABEL = 1,		/* process or user clearance */
	M_INTERNAL = 2,		/* internal form for use in public databases */
	M_COLOR = 3,		/* process label color */
	PRINTER_TOP_BOTTOM = 4,	/* DIA banner page top/bottom */
	PRINTER_LABEL = 5,	/* DIA banner page label */
	PRINTER_CAVEATS = 6,	/* DIA banner page caveats */
	PRINTER_CHANNELS = 7	/* DIA banner page handling channels */
} m_label_str_t;

/* Flags for conversion, not all flags apply to all types */
#define	DEF_NAMES	0x1
#define	SHORT_NAMES	0x3	/* short names are prefered where defined */
#define	LONG_NAMES	0x4	/* long names are prefered where defined */

extern int label_to_str(const m_label_t *, char **, const m_label_str_t,
    uint_t);
extern int l_to_str_internal(const m_label_t *, char **);

/* Parsing types */
typedef enum _m_label_type {
	MAC_LABEL = 1,		/* process or object label */
	USER_CLEAR = 2		/* user's clearance (LUB) */
} m_label_type_t;

/* Flags for parsing */

#define	L_DEFAULT		0x0
#define	L_MODIFY_EXISTING	0x1	/* start parsing with existing label */
#define	L_NO_CORRECTION		0x2	/* must be correct by l_e rules */
#define	L_CHECK_AR		0x10	/* must be in l_e AR */

/* EINVAL sub codes */

#define	M_OUTSIDE_AR		-4	/* not in l_e AR */
#define	M_BAD_STRING		-3	/* DIA L_BAD_LABEL */
	/* bad requested label type, bad previous label type */
#define	M_BAD_LABEL		-2	/* DIA L_BAD_CLASSIFICATION, */

extern int str_to_label(const char *, m_label_t **, const m_label_type_t,
    uint_t, int *);
extern int hexstr_to_label(const char *, m_label_t *);

extern m_label_t *m_label_alloc(const m_label_type_t);

extern int m_label_dup(m_label_t **, const m_label_t *);

extern void m_label_free(m_label_t *);

/* Contract Private interfaces with the label builder GUIs */

extern int	bslcvtfull(const m_label_t *, const m_range_t *, int,
    char **, char **[], char **[], char *[], int *, int *);
extern int	bslcvt(const m_label_t *, int, char **, char *[]);
extern int	bclearcvtfull(const m_label_t *, const m_range_t *, int,
    char **, char **[], char **[], char *[], int *, int *);
extern int	bclearcvt(const m_label_t *, int, char **, char *[]);

extern int	labelfields(struct name_fields *);
extern int	userdefs(m_label_t *, m_label_t *);
extern int	zonecopy(m_label_t *, char *, char *, char *, int);

#ifdef DEBUG
/* testing hook: see devfsadm.c, mkdevalloc.c and allocate.c */
#define	is_system_labeled_debug(statbufp)	\
	((stat("/ALLOCATE_FORCE_LABEL", (statbufp)) == 0) ? 1 : 0)
#else	/* DEBUG */
#define	is_system_labeled_debug(statbufp)	0
#endif	/* DEBUG */

#endif	/* !_TSOL_LABEL_H */
