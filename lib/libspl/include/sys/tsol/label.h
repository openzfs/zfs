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

#ifndef	_SYS_TSOL_LABEL_H
#define	_SYS_TSOL_LABEL_H

#include <sys/types.h>
#ifdef _KERNEL
#include <sys/cred.h>
#include <sys/vnode.h>
#include <sys/tsol/label_macro.h>
#endif /* _KERNEL */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * types of label comparison
 */
#define	EQUALITY_CHECK	0
#define	DOMINANCE_CHECK	1

/* Manifest human readable label names */
#define	ADMIN_LOW	"ADMIN_LOW"
#define	ADMIN_HIGH	"ADMIN_HIGH"

/* Binary Label Structure Definitions */

typedef	struct _mac_label_impl	m_label_t;

typedef m_label_t	blevel_t,		/* compatibility */
			bslabel_t,		/* Sensitivity Label */
			bclear_t;		/* Clearance */

typedef struct _tsol_binary_level_lrange {	/* Level Range */
	m_label_t *lower_bound;
	m_label_t *upper_bound;
} m_range_t;

typedef	m_range_t	blrange_t;

typedef struct tsol_mlp_s {
	uchar_t mlp_ipp;
	uint16_t mlp_port;
	uint16_t mlp_port_upper;
} tsol_mlp_t;

/* Procedure Interface Definitions available to user and kernel */

extern int	bltype(const void *, uint8_t);
extern int	blequal(const m_label_t *, const m_label_t *);
extern int	bldominates(const m_label_t *, const m_label_t *);
extern int	blstrictdom(const m_label_t *, const m_label_t *);
extern int	blinrange(const m_label_t *, const m_range_t *);
extern void	blmaximum(m_label_t *, const m_label_t *);
extern void	blminimum(m_label_t *, const m_label_t *);
extern void	bsllow(m_label_t *);
extern void	bslhigh(m_label_t *);
extern void	bclearlow(m_label_t *);
extern void	bclearhigh(m_label_t *);
extern void	bslundef(m_label_t *);
extern void	bclearundef(m_label_t *);
extern void	setbltype(void *, uint8_t);
extern boolean_t	bisinvalid(const void *);

#ifdef	_KERNEL
typedef struct tsol_mlp_entry_s {
	struct tsol_mlp_entry_s *mlpe_next, *mlpe_prev;
	zoneid_t mlpe_zoneid;
	tsol_mlp_t mlpe_mlp;
} tsol_mlp_entry_t;

typedef struct tsol_mlp_list_s {
	krwlock_t mlpl_rwlock;
	tsol_mlp_entry_t *mlpl_first, *mlpl_last;
} tsol_mlp_list_t;

typedef	struct ts_label_s {
	uint_t		tsl_ref;	/* Reference count */
	uint32_t	tsl_doi;	/* Domain of Interpretation */
	uint32_t	tsl_flags;	/* TSLF_* below */
	m_label_t	tsl_label;	/* Actual label */
} ts_label_t;

#define	DEFAULT_DOI 1

/*
 * TSLF_UNLABELED is set in tsl_flags for  packets with no explicit label
 * when the peer is unlabeled.
 *
 * TSLF_IMPLICIT_IN is set when a packet is received with no explicit label
 * from a peer which is flagged in the tnrhdb as label-aware.
 *
 * TSLF_IMPLICIT_OUT is set when the packet should be sent without an
 * explict label even if the peer or next-hop router is flagged in the
 * tnrhdb as label-aware.
 */

#define	TSLF_UNLABELED		0x00000001	/* peer is unlabeled */
#define	TSLF_IMPLICIT_IN	0x00000002	/* inbound implicit */
#define	TSLF_IMPLICIT_OUT	0x00000004	/* outbound implicit */

#define	CR_SL(cr)	(label2bslabel(crgetlabel(cr)))

extern ts_label_t	*l_admin_low;
extern ts_label_t	*l_admin_high;
extern uint32_t		default_doi;
extern int		sys_labeling;

extern void		label_init(void);
extern ts_label_t	*labelalloc(const m_label_t *, uint32_t, int);
extern ts_label_t	*labeldup(const ts_label_t *, int);
extern void		label_hold(ts_label_t *);
extern void		label_rele(ts_label_t *);
extern m_label_t	*label2bslabel(ts_label_t *);
extern uint32_t		label2doi(ts_label_t *);
extern boolean_t	label_equal(const ts_label_t *, const ts_label_t *);
extern cred_t 		*newcred_from_bslabel(m_label_t *, uint32_t, int);
extern cred_t 		*copycred_from_bslabel(const cred_t *, m_label_t *,
			    uint32_t, int);
extern cred_t		*copycred_from_tslabel(const cred_t *, ts_label_t *,
			    int);
extern ts_label_t	*getflabel(vnode_t *);
extern int		getlabel(const char *, m_label_t *);
extern int		fgetlabel(int, m_label_t *);
extern int		_blinrange(const m_label_t *, const brange_t *);
extern int		blinlset(const m_label_t *, const blset_t);

extern int		l_to_str_internal(const m_label_t *, char **);
extern int		hexstr_to_label(const char *, m_label_t *);

/*
 * The use of '!!' here prevents users from referencing this function-like
 * macro as though it were an l-value, and in normal use is optimized away
 * by the compiler.
 */
#define	is_system_labeled()	(!!(sys_labeling > 0))

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* !_SYS_TSOL_LABEL_H */
