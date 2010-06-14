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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_LABEL_MACRO_H
#define	_LABEL_MACRO_H

#include <sys/types.h>

/* PRIVATE ONLY TO THE LABEL LIBRARY.  DO NOT USE ELSEWHERE */

/* Actual Binary Label Structure Definitions */

typedef int16_t	_Classification;
typedef struct {
	union {
		uint8_t		class_ar[2];
		_Classification	class_chunk;
	} class_u;
} Classification_t;

typedef struct {
	uint32_t c1;
	uint32_t c2;
	uint32_t c3;
	uint32_t c4;
	uint32_t c5;
	uint32_t c6;
	uint32_t c7;
	uint32_t c8;
} Compartments_t;

typedef struct {
	uint32_t m1;
	uint32_t m2;
	uint32_t m3;
	uint32_t m4;
	uint32_t m5;
	uint32_t m6;
	uint32_t m7;
	uint32_t m8;
} Markings_t;

typedef struct _mac_label_impl {
	uint8_t id;		/* Magic to say label type */
	uint8_t _c_len;		/* Number of Compartment words */
	Classification_t classification;
	Compartments_t compartments;
} _mac_label_impl_t;

typedef _mac_label_impl_t	_blevel_impl_t,		/* compatibility */
				_bslabel_impl_t,	/* Sensitivity Label */
				_bclear_impl_t;		/* Clearance */

typedef struct _binary_information_label_impl {	/* Information Label */
	_mac_label_impl_t	binformation_level;
	Markings_t markings;
} _bilabel_impl_t;

typedef struct _binary_cmw_label_impl {		/* CMW Label */
	_bslabel_impl_t bcl_sensitivity_label;
	_bilabel_impl_t bcl_information_label;
} _bclabel_impl_t;

typedef struct _binary_level_range_impl {	/* Level Range */
	_mac_label_impl_t lower_bound;
	_mac_label_impl_t upper_bound;
} _brange_impl_t, brange_t;

#define	NMLP_MAX	0x10
#define	NSLS_MAX	0x4

typedef _mac_label_impl_t blset_t[NSLS_MAX];

/* Label Identifier Types */

#define	SUN_MAC_ID	0x41	/* MAC label, legacy SUN_SL_ID */
#define	SUN_UCLR_ID	0x49	/* User Clearance, legacy SUN_CLR_ID */

#define	_C_LEN		8	/* number of compartments words */

/* m_label_t macros */
#define	_MTYPE(l, t) \
	(((_mac_label_impl_t *)(l))->id == (t))

#define	_MSETTYPE(l, t) \
	(((_mac_label_impl_t *)(l))->id = (t))

#define	_MGETTYPE(l)	(((_mac_label_impl_t *)(l))->id)

#define	_MEQUAL(l1, l2) \
	(LCLASS(l1) == LCLASS(l2) && \
	(l1)->_comps.c1 == (l2)->_comps.c1 && \
	(l1)->_comps.c2 == (l2)->_comps.c2 && \
	(l1)->_comps.c3 == (l2)->_comps.c3 && \
	(l1)->_comps.c4 == (l2)->_comps.c4 && \
	(l1)->_comps.c5 == (l2)->_comps.c5 && \
	(l1)->_comps.c6 == (l2)->_comps.c6 && \
	(l1)->_comps.c7 == (l2)->_comps.c7 && \
	(l1)->_comps.c8 == (l2)->_comps.c8)

#define	SUN_INVALID_ID	0	/* uninitialized label */
#define	SUN_CMW_ID	0x83	/* 104 - total bytes in CMW Label */
#define	SUN_SL_ID	0x41	/* 36 - total bytes in Sensitivity Label */
#define	SUN_SL_UN	0xF1	/* undefined Sensitivity Label */
#define	SUN_IL_ID	0x42	/* 68 - total bytes in Information Label */
#define	SUN_IL_UN	0x73	/* undefined Information Label */
#define	SUN_CLR_ID	0x49	/* 36 - total bytes in Clearance */
#define	SUN_CLR_UN	0xF9	/* undefined Clearance */

#define	_bcl_sl		bcl_sensitivity_label
#define	_bcl_il		bcl_information_label
#define	_bslev_il	binformation_level

#define	_lclass		classification
#ifdef	_BIG_ENDIAN
#define	LCLASS(slp)	((slp)->_lclass.class_u.class_chunk)
#define	LCLASS_SET(slp, l)	((slp)->_lclass.class_u.class_chunk = (l))
#else
#define	LCLASS(slp)	\
	((_Classification)(((slp)->_lclass.class_u.class_ar[0] << 8) | \
	(slp)->_lclass.class_u.class_ar[1]))
#define	LCLASS_SET(slp, l)	\
	((slp)->_lclass.class_u.class_ar[0] = (uint8_t)((l)>> 8), \
	(slp)->_lclass.class_u.class_ar[1] = (uint8_t)(l))
#endif	/* _BIG_ENDIAN */
#define	_comps		compartments

#define	_iid		_bslev_il.id
#define	_i_c_len		_bslev_il._c_len
#define	_iclass		_bslev_il._lclass
#ifdef	_BIG_ENDIAN
#define	ICLASS(ilp)	((ilp)->_iclass.class_u.class_chunk)
#define	ICLASS_SET(ilp, l)	((ilp)->_iclass.class_u.class_chunk = (l))
#else
#define	ICLASS(ilp)	\
	((_Classification)(((ilp)->_iclass.class_u.class_ar[0] << 8) | \
	(ilp)->_iclass.class_u.class_ar[1]))
#define	ICLASS_SET(ilp, l)	\
	((ilp)->_iclass.class_u.class_ar[0] = (uint8_t)((l)>> 8), \
	(ilp)->_iclass.class_u.class_ar[1] = (uint8_t)(l))
#endif	/* _BIG_ENDIAN */
#define	_icomps		_bslev_il._comps
#define	_imarks		markings

/* Manifest Constant Values */

#define	LOW_CLASS	0	/* Admin_Low classification value */
#define	HIGH_CLASS	0x7FFF	/* Admin_High classification value */
#define	EMPTY_SET	0	/* Empty compartments and markings set */
#define	UNIVERSAL_SET	0xFFFFFFFFU	/* Universal compartments and */
					/* markings set */

/* Construct initial labels */

#define	_LOW_LABEL(l, t) \
	((l)->id = t, (l)->_c_len = _C_LEN, LCLASS_SET(l, LOW_CLASS), \
	(l)->_comps.c1 = (l)->_comps.c2 = (l)->_comps.c3 = (l)->_comps.c4 = \
	(l)->_comps.c5 = (l)->_comps.c6 = (l)->_comps.c7 = (l)->_comps.c8 = \
	EMPTY_SET)

#define	_HIGH_LABEL(l, t) \
	((l)->id = t, (l)->_c_len = _C_LEN, LCLASS_SET(l, HIGH_CLASS), \
	(l)->_comps.c1 = (l)->_comps.c2 = (l)->_comps.c3 = (l)->_comps.c4 = \
	(l)->_comps.c5 = (l)->_comps.c6 = (l)->_comps.c7 = (l)->_comps.c8 = \
	UNIVERSAL_SET)

/* Macro equivalents */

/* Is this memory a properly formatted label of type t? */
#define	BLTYPE(l, t) \
	((t) == SUN_CMW_ID ? \
	(((_bclabel_impl_t *)(l))->_bcl_sl.id == SUN_SL_ID || \
	((_bclabel_impl_t *)(l))->_bcl_sl.id == SUN_SL_UN) && \
	(((_bclabel_impl_t *)(l))->_bcl_il._iid == SUN_IL_ID || \
	((_bclabel_impl_t *)(l))->_bcl_il._iid == SUN_IL_UN) : \
	((_mac_label_impl_t *)(l))->id == (t))

/* Are the levels of these labels equal? */
#define	BLEQUAL(l1, l2) \
	_BLEQUAL((_mac_label_impl_t *)(l1), (_mac_label_impl_t *)(l2))

#define	_BLEQUAL(l1, l2) \
	(LCLASS(l1) == LCLASS(l2) && \
	(l1)->_comps.c1 == (l2)->_comps.c1 && \
	(l1)->_comps.c2 == (l2)->_comps.c2 && \
	(l1)->_comps.c3 == (l2)->_comps.c3 && \
	(l1)->_comps.c4 == (l2)->_comps.c4 && \
	(l1)->_comps.c5 == (l2)->_comps.c5 && \
	(l1)->_comps.c6 == (l2)->_comps.c6 && \
	(l1)->_comps.c7 == (l2)->_comps.c7 && \
	(l1)->_comps.c8 == (l2)->_comps.c8)

/* Does the level of l1 dominate that of l2? */
#define	BLDOMINATES(l1, l2) \
	_BLDOMINATES((_mac_label_impl_t *)(l1), (_mac_label_impl_t *)(l2))

#define	_BLDOMINATES(l1, l2) (LCLASS(l1) >= LCLASS(l2) && \
	(l2)->_comps.c1 == ((l1)->_comps.c1 & (l2)->_comps.c1) && \
	(l2)->_comps.c2 == ((l1)->_comps.c2 & (l2)->_comps.c2) && \
	(l2)->_comps.c3 == ((l1)->_comps.c3 & (l2)->_comps.c3) && \
	(l2)->_comps.c4 == ((l1)->_comps.c4 & (l2)->_comps.c4) && \
	(l2)->_comps.c5 == ((l1)->_comps.c5 & (l2)->_comps.c5) && \
	(l2)->_comps.c6 == ((l1)->_comps.c6 & (l2)->_comps.c6) && \
	(l2)->_comps.c7 == ((l1)->_comps.c7 & (l2)->_comps.c7) && \
	(l2)->_comps.c8 == ((l1)->_comps.c8 & (l2)->_comps.c8))

/* Does the level of l1 strictly dominate that of l2? */
#define	BLSTRICTDOM(l1, l2) (!BLEQUAL(l1, l2) && BLDOMINATES(l1, l2))

/* Is the level of l within the range r? */
#define	BLINRANGE(l, r)\
	(BLDOMINATES((l), &((r)->lower_bound)) && \
	BLDOMINATES(&((r)->upper_bound), (l)))

/* Least Upper Bound level l1 and l2 replacing l1 with the result. */
#define	BLMAXIMUM(l1, l2) \
	_BLMAXIMUM((_mac_label_impl_t *)(l1), (_mac_label_impl_t *)(l2))

#define	_BLMAXIMUM(l1, l2)\
	(((l1)->_lclass = (LCLASS(l1) < LCLASS(l2)) ? \
	(l2)->_lclass : (l1)->_lclass), \
	(l1)->_comps.c1 |= (l2)->_comps.c1, \
	(l1)->_comps.c2 |= (l2)->_comps.c2, \
	(l1)->_comps.c3 |= (l2)->_comps.c3, \
	(l1)->_comps.c4 |= (l2)->_comps.c4, \
	(l1)->_comps.c5 |= (l2)->_comps.c5, \
	(l1)->_comps.c6 |= (l2)->_comps.c6, \
	(l1)->_comps.c7 |= (l2)->_comps.c7, \
	(l1)->_comps.c8 |= (l2)->_comps.c8)

/* Greatest Lower Bound level l1 and l2 replacing l1 with the result. */
#define	BLMINIMUM(l1, l2) \
	_BLMINIMUM((_mac_label_impl_t *)(l1), (_mac_label_impl_t *)(l2))

#define	_BLMINIMUM(l1, l2)\
	(((l1)->_lclass = (LCLASS(l1) > LCLASS(l2)) ? \
	(l2)->_lclass : (l1)->_lclass), \
	(l1)->_comps.c1 &= (l2)->_comps.c1, \
	(l1)->_comps.c2 &= (l2)->_comps.c2, \
	(l1)->_comps.c3 &= (l2)->_comps.c3, \
	(l1)->_comps.c4 &= (l2)->_comps.c4, \
	(l1)->_comps.c5 &= (l2)->_comps.c5, \
	(l1)->_comps.c6 &= (l2)->_comps.c6, \
	(l1)->_comps.c7 &= (l2)->_comps.c7, \
	(l1)->_comps.c8 &= (l2)->_comps.c8)

/* Create Manifest Labels */

/* Write a System_Low CMW Label into this memory. */
#define	BCLLOW(l) (BSLLOW(BCLTOSL(l)), BILLOW(BCLTOIL(l)))

/* Write a System_Low Sensitivity Label into this memory. */
#define	BSLLOW(l) _BSLLOW((_bslabel_impl_t *)(l))

#define	_BSLLOW(l) \
	((l)->id = SUN_SL_ID, (l)->_c_len = _C_LEN, LCLASS_SET(l, LOW_CLASS), \
	(l)->_comps.c1 = (l)->_comps.c2 = (l)->_comps.c3 = (l)->_comps.c4 = \
	(l)->_comps.c5 = (l)->_comps.c6 = (l)->_comps.c7 = (l)->_comps.c8 = \
	EMPTY_SET)

/* Write a System_High Sensitivity Label into this memory. */
#define	BSLHIGH(l) _BSLHIGH((_bslabel_impl_t *)(l))

#define	_BSLHIGH(l) \
	((l)->id = SUN_SL_ID, (l)->_c_len = _C_LEN, LCLASS_SET(l, HIGH_CLASS), \
	(l)->_comps.c1 = (l)->_comps.c2 = (l)->_comps.c3 = (l)->_comps.c4 = \
	(l)->_comps.c5 = (l)->_comps.c6 = (l)->_comps.c7 = (l)->_comps.c8 = \
	UNIVERSAL_SET)

/* Write a System_Low Information Label into this memory. */
#define	BILLOW(l) _BILLOW((_bilabel_impl_t *)(l))

#define	_BILLOW(l) \
	((l)->_iid = SUN_IL_ID, (l)->_i_c_len = _C_LEN, \
	ICLASS_SET(l, LOW_CLASS), \
	(l)->_icomps.c1 = (l)->_icomps.c2 = (l)->_icomps.c3 = \
	(l)->_icomps.c4 = (l)->_icomps.c5 = (l)->_icomps.c6 = \
	(l)->_icomps.c7 = (l)->_icomps.c8 = EMPTY_SET, \
	(l)->_imarks.m1 = (l)->_imarks.m2 = (l)->_imarks.m3 = \
	(l)->_imarks.m4 = (l)->_imarks.m5 = (l)->_imarks.m6 = \
	(l)->_imarks.m7 = (l)->_imarks.m8 = EMPTY_SET)


/* Write a System_Low Sensitivity Label into this memory. */
#define	BCLEARLOW(l) _BCLEARLOW((_bclear_impl_t *)(l))

#define	_BCLEARLOW(c) \
	((c)->id = SUN_CLR_ID, (c)->_c_len = _C_LEN, \
	LCLASS_SET(c, LOW_CLASS), \
	(c)->_comps.c1 = (c)->_comps.c2 = (c)->_comps.c3 = (c)->_comps.c4 = \
	(c)->_comps.c5 = (c)->_comps.c6 = (c)->_comps.c7 = (c)->_comps.c8 = \
	EMPTY_SET)

/* Write a System_High Sensitivity Label into this memory. */
#define	BCLEARHIGH(l) _BCLEARHIGH((_bclear_impl_t *)(l))

#define	_BCLEARHIGH(c) \
	((c)->id = SUN_CLR_ID, (c)->_c_len = _C_LEN, \
	LCLASS_SET(c, HIGH_CLASS), \
	(c)->_comps.c1 = (c)->_comps.c2 = (c)->_comps.c3 = (c)->_comps.c4 = \
	(c)->_comps.c5 = (c)->_comps.c6 = (c)->_comps.c7 = (c)->_comps.c8 = \
	UNIVERSAL_SET)

/* Write an undefined Sensitivity Label into this memory. */
#define	BSLUNDEF(l) (((_bslabel_impl_t *)(l))->id = SUN_SL_UN)

/* Write an undefined Clearance into this memory. */
#define	BCLEARUNDEF(c) (((_bclear_impl_t *)(c))->id = SUN_CLR_UN)

/* Retrieve the Sensitivity Label portion of a CMW Label */
#define	BCLTOSL(l) ((bslabel_t *)&((_bclabel_impl_t *)(l))->_bcl_sl)

/* Retrieve the Information Label portion of a CMW Label */
#define	BCLTOIL(l) ((_bilabel_impl_t *)&((_bclabel_impl_t *)(l))->_bcl_il)

/* Copy the Sensitivity Label portion from a CMW Label */
#define	GETCSL(l1, l2) \
	(*((_bslabel_impl_t *)(l1)) = ((_bclabel_impl_t *)(l2))->_bcl_sl)

/* Replace the Sensitivity Label portion of a CMW Label */
#define	SETCSL(l1, l2) \
	(((_bclabel_impl_t *)(l1))->_bcl_sl = *((_bslabel_impl_t *)(l2)))

/* Set type of this memory to the label type 't' */
#define	SETBLTYPE(l, t) (((_bclabel_impl_t *)(l))->_bcl_sl.id = (t))

#define	GETBLTYPE(l)	(((const _bclabel_impl_t *)(l))->_bcl_sl.id)

#endif	/* !_LABEL_MACRO_H */
