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

#include <errno.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/tsol/label_macro.h>
#include <sys/tsol/label.h>
#include "label.h"
#include "labeld.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>



static _mac_label_impl_t low;
static _mac_label_impl_t high;
static int	inited = 0;

#define	freeit(a, l)		free(a)

/* 0x + Classification + '-' + ll + '-' + Compartments + end of string */
#define	_HEX_SIZE 2+(sizeof (Classification_t)*2)+4+\
	(sizeof (Compartments_t)*2)+1

/* 0x + Classification + '-' + ll + '-' + end of string */
#define	_MIN_HEX (2 + (sizeof (Classification_t)*2) + 4 + 1)

static char digits[] = "0123456789abcdef";

#define	HEX(h, i, l, s) \
	for (; i < s; /* */) {\
	h[i++] = digits[(unsigned int)(*l >> 4)];\
	h[i++] = digits[(unsigned int)(*l++&0xF)]; }

static int
__hex(char **s, const m_label_t *l)
{
	char	*hex;
	int	i = 0;
	uchar_t *hl;
	int	hex_len;
	uchar_t *len;

	hl = (uchar_t  *)&(((_mac_label_impl_t *)l)->_c_len);
	len = hl;

	if (*len == 0) {
		/* old binary label */
		hex_len = _HEX_SIZE;
	} else {
		hex_len = _MIN_HEX + (*len * sizeof (uint32_t) * 2);
	}

	if ((hex = malloc(hex_len)) == NULL) {
		return (-1);
	}

	/* header */

	hex[i++] = '0';
	hex[i++] = 'x';

	/* classification */

	hl++;		/* start at classification */
	HEX(hex, i, hl, 6);

	/* Add compartments length */
	hex[i++] = '-';
	HEX(hex, i, len, 9);
	hex[i++] = '-';

	/* compartments */
	HEX(hex, i, hl, hex_len-1);
	hex[i] = '\0';

	/* truncate trailing zeros */

	while (hex[i-1] == '0' && hex[i-2] == '0') {
		i -= 2;
	}
	hex[i] = '\0';

	if ((*s = strdup(hex)) == NULL) {
		freeit(hex, hex_len);
		return (-1);
	}

	freeit(hex, hex_len);
	return (0);

}

int
l_to_str_internal(const m_label_t *l, char **s)
{
	if (inited == 0) {
		inited = 1;
		_BSLLOW(&low);
		_BSLHIGH(&high);
	}

	if (!(_MTYPE(l, SUN_MAC_ID) || _MTYPE(l, SUN_UCLR_ID))) {
		errno = EINVAL;
		*s = NULL;
		return (-1);
	}
	if (_MEQUAL(&low, (_mac_label_impl_t *)l)) {
		if ((*s = strdup(ADMIN_LOW)) == NULL) {
			return (-1);
		}
		return (0);
	}
	if (_MEQUAL(&high, (_mac_label_impl_t *)l)) {
		if ((*s = strdup(ADMIN_HIGH)) == NULL) {
			return (-1);
		}
		return (0);
	}

	return (__hex(s, l));
}

/*
 * label_to_str -- convert a label to the requested type of string.
 *
 *	Entry	l = label to convert;
 *		t = type of conversion;
 *		f = flags for conversion type;
 *
 *	Exit	*s = allocated converted string;
 *		     Caller must call free() to free.
 *
 *	Returns	0, success.
 *		-1, error, errno set; *s = NULL.
 *
 *	Calls	labeld
 */

int
label_to_str(const m_label_t *l, char **s, const m_label_str_t t, uint_t f)
{
	labeld_data_t	call;
	labeld_data_t	*callp = &call;
	size_t	bufsize = sizeof (labeld_data_t);
	size_t	datasize;
	int	err;
	int	string_start = 0;

	if (inited == 0) {
		inited = 1;
		_BSLLOW(&low);
		_BSLHIGH(&high);
	}

#define	lscall callp->param.acall.cargs.ls_arg
#define	lsret callp->param.aret.rvals.ls_ret
	switch (t) {
	case M_LABEL:
		call.callop = LTOS;
		lscall.label = *l;
		lscall.flags = f;
		datasize = CALL_SIZE(ls_call_t, 0);
		if ((err = __call_labeld(&callp, &bufsize, &datasize)) ==
		    SUCCESS) {
			if (callp->reterr != 0) {
				errno = EINVAL;
				*s = NULL;
				return (-1);
			}
			*s = strdup(lsret.buf);
			if (callp != &call) {
				/* release returned buffer */
				(void) munmap((void *)callp, bufsize);
			}
			if (*s == NULL) {
				return (-1);
			}
			return (0);
		}
		switch (err) {
		case NOSERVER:
			/* server not present */
			/* special case admin_low and admin_high */

			if (_MEQUAL(&low, (_mac_label_impl_t *)l)) {
				if ((*s = strdup(ADMIN_LOW)) == NULL) {
					return (-1);
				}
				return (0);
			} else if (_MEQUAL(&high, (_mac_label_impl_t *)l)) {
				if ((*s = strdup(ADMIN_HIGH)) == NULL) {
					return (-1);
				}
				return (0);
			}
			errno = ENOTSUP;
			break;
		default:
			errno = EINVAL;
			break;
		}
		*s = NULL;
		return (-1);
#undef	lscall
#undef	lsret

	case M_INTERNAL: {
		return (l_to_str_internal(l, s));
	}

#define	ccall callp->param.acall.cargs.color_arg
#define	cret callp->param.aret.rvals.color_ret
	case M_COLOR:
		datasize = CALL_SIZE(color_call_t, 0);
		call.callop = BLTOCOLOR;
		ccall.label = *l;

		if (__call_labeld(&callp, &bufsize, &datasize) == SUCCESS) {
			if (callp->reterr != 0) {
				errno = EINVAL;
				*s = NULL;
				return (-1);
			}
			*s = strdup(cret.color);
			if (callp != &call) {
				/* release returned buffer */
				(void) munmap((void *)callp, bufsize);
			}
			if (*s == NULL) {
				return (-1);
			}
			return (0);
		} else {
			errno = ENOTSUP;
			*s = NULL;
			return (-1);
		}
#undef	ccall
#undef	cret

#define	prcall	callp->param.acall.cargs.pr_arg
#define	prret	callp->param.aret.rvals.pr_ret
	case PRINTER_TOP_BOTTOM:
		call.callop = PR_TOP;
		break;
	case PRINTER_LABEL:
		call.callop = PR_LABEL;
		break;
	case PRINTER_CAVEATS:
		call.callop = PR_CAVEATS;
		string_start = 1;	/* compensate for leading space */
		break;
	case PRINTER_CHANNELS:
		call.callop = PR_CHANNELS;
		string_start = 1;	/* compensate for leading space */
		break;
	default:
		errno = EINVAL;
		*s = NULL;
		return (-1);
	}
	/* do the common printer calls */
	datasize = CALL_SIZE(pr_call_t, 0);
	prcall.label = *l;
	prcall.flags = f;
	if (__call_labeld(&callp, &bufsize, &datasize) == SUCCESS) {
		if (callp->reterr != 0) {
			errno = EINVAL;
			*s = NULL;
			return (-1);
		}
		*s = strdup(&prret.buf[string_start]);
		if (callp != &call) {
			/* release returned buffer */
			(void) munmap((void *)callp, bufsize);
		}
		if (*s == NULL) {
			return (-1);
		}
		return (0);
	} else {
		errno = ENOTSUP;
		*s = NULL;
		return (-1);
	}
#undef	prcall
#undef	prret
}
