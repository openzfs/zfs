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
 * Copyright (c) 1990, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2012 Garrett D'Amore <garrett@damore.org>. All rights reserved.
 * Copyright (c) 2012 by Delphix. All rights reserved.
 */



#ifndef _SPL_SUNDDI_H
#define	_SPL_SUNDDI_H

#include <sys/cred.h>
#include <sys/uio.h>
#include <sys/mutex.h>
#include <sys/u8_textprep.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <libkern/libkern.h>

typedef int ddi_devid_t;

#define	DDI_DEV_T_NONE				((dev_t)-1)
#define	DDI_DEV_T_ANY				((dev_t)-2)
#define	DI_MAJOR_T_UNKNOWN			((major_t)0)

#define	DDI_PROP_DONTPASS			0x0001
#define	DDI_PROP_CANSLEEP			0x0002

#define	DDI_SUCCESS				0
#define	DDI_FAILURE				-1

#define	ddi_prop_lookup_string(x1, x2, x3, x4, x5)	(*x5 = NULL)
#define	ddi_prop_free(x)			(void)0
#define	ddi_root_node()				(void)0

#define	isalnum(ch)	(isalpha(ch) || isdigit(ch))
#define	isalpha(ch)	(isupper(ch) || islower(ch))
#define	isdigit(ch)	((ch) >= '0' && (ch) <= '9')
#define	islower(ch)	((ch) >= 'a' && (ch) <= 'z')
#define	isspace(ch)	(((ch) == ' ') || ((ch) == '\r') || ((ch) == '\n') || \
	    ((ch) == '\t') || ((ch) == '\f'))
#define	isupper(ch)	((ch) >= 'A' && (ch) <= 'Z')
#define	isxdigit(ch)	(isdigit(ch) || ((ch) >= 'a' && (ch) <= 'f') || \
	    ((ch) >= 'A' && (ch) <= 'F'))
#define	tolower(C)	(((C) >= 'A' && (C) <= 'Z') ? (C) - 'A' + 'a' : (C))
#define	toupper(C)	(((C) >= 'a' && (C) <= 'z') ? (C) - 'a' + 'A': (C))
#define	isgraph(C)	((C) >= 0x21 && (C) <= 0x7E)
#define	ispunct(C)	(((C) >= 0x21 && (C) <= 0x2F) || \
	    ((C) >= 0x3A && (C) <= 0x40) ||		 \
	    ((C) >= 0x5B && (C) <= 0x60) ||		 \
	    ((C) >= 0x7B && (C) <= 0x7E))

// Define proper Solaris API calls, and clean ZFS up to use
int ddi_copyin(const void *from, void *to, size_t len, int flags);
int ddi_copyout(const void *from, void *to, size_t len, int flags);
int ddi_copyinstr(const void *uaddr, void *kaddr, size_t len, size_t *done);

static inline int
ddi_strtol(const char *str, char **nptr, int base, long *result)
{
	*result = strtol(str, nptr, base);
	if (*result == 0)
		return (EINVAL);
	else if (*result == LONG_MIN || *result == LONG_MAX)
		return (ERANGE);
	return (0);
}

static inline int
ddi_strtoul(const char *str, char **nptr, int base, unsigned long *result)
{
	*result = strtoul(str, nptr, base);
	if (*result == 0)
		return (EINVAL);
	else if (*result == ULONG_MAX)
		return (ERANGE);
	return (0);
}

static inline int
ddi_strtoull(const char *str, char **nptr, int base,
    unsigned long long *result)
{
	*result = (unsigned long long)strtouq(str, nptr, base);
	if (*result == 0)
		return (EINVAL);
	else if (*result == ULLONG_MAX)
		return (ERANGE);
	return (0);
}

static inline int
ddi_strtoll(const char *str, char **nptr, int base, long long *result)
{
	*result = (unsigned long long)strtoq(str, nptr, base);
	if (*result == 0)
		return (EINVAL);
	else if (*result == ULLONG_MAX)
		return (ERANGE);
	return (0);
}

#ifndef OTYPCNT
#define	OTYPCNT		5
#define	OTYP_BLK	0
#define	OTYP_MNT	1
#define	OTYP_CHR	2
#define	OTYP_SWP	3
#define	OTYP_LYR	4
#endif

#define	P2END(x, align)			(-(~(x) & -(align)))

#define	ddi_name_to_major(name) devsw_name2blk(name, NULL, 0)

struct dev_info {
    dev_t dev;   // Major / Minor
    void *devc;
    void *devb;
};
typedef struct dev_info dev_info_t;


int	ddi_strtoul(const char *, char **, int, unsigned long *);
int	ddi_strtol(const char *, char **, int, long *);
int	ddi_soft_state_init(void **, size_t, size_t);
int	ddi_soft_state_zalloc(void *, int);
void	*ddi_get_soft_state(void *, int);
void	ddi_soft_state_free(void *, int);
void	ddi_soft_state_fini(void **);
int	ddi_create_minor_node(dev_info_t *, char *, int,
    minor_t, char *, int);
void	ddi_remove_minor_node(dev_info_t *, char *);

int ddi_driver_major(dev_info_t *);

typedef void 	*ldi_ident_t;

#define	DDI_SUCCESS	0
#define	DDI_FAILURE	-1

#define	DDI_PSEUDO	""

#define	ddi_prop_update_int64(a, b, c, d)	DDI_SUCCESS
#define	ddi_prop_update_string(a, b, c, d)	DDI_SUCCESS

#define	bioerror(bp, er)	(buf_seterror((bp), (er)))
#define	biodone(bp) buf_biodone(bp)

#define	ddi_ffs ffs
static inline long ddi_fls(long mask) {			\
	/* Algorithm courtesy of Steve Chessin. */	\
    while (mask) {					\
		long nx;				\
		if ((nx = (mask & (mask - 1))) == 0) {	\
			break;				\
		}                                       \
		mask = nx;				\
	}						\
	return (ffs(mask));				\
}

#define	getminor(X) minor((X))



/*
 * This data structure is entirely private to the soft state allocator.
 */
struct i_ddi_soft_state {
	void		**array;	/* the array of pointers */
	kmutex_t	lock;	/* serialize access to this struct */
	size_t		size;	/* how many bytes per state struct */
	size_t		n_items;	/* how many structs herein */
	struct i_ddi_soft_state *next;	/* 'dirty' elements */
};

#define	MIN_N_ITEMS	8	/* 8 void *'s == 32 bytes */

extern int strspn(const char *string, char *charset);


#endif /* SPL_SUNDDI_H */
