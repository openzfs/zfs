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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/tsol/label_macro.h>
#include <sys/tsol/label.h>
#include <label.h>
#include <labeld.h>

#define	IS_LOW(s) \
	((strncasecmp(s, ADMIN_LOW, (sizeof (ADMIN_LOW) - 1)) == 0) && \
	(s[sizeof (ADMIN_LOW) - 1] == '\0'))
#define	IS_HIGH(s) \
	((strncasecmp(s, ADMIN_HIGH, (sizeof (ADMIN_HIGH) - 1)) == 0) && \
	(s[sizeof (ADMIN_HIGH) - 1] == '\0'))
#define	IS_HEX(f, s) \
	(((((f) == L_NO_CORRECTION)) || ((f) == L_DEFAULT)) && \
	(((s)[0] == '0') && (((s)[1] == 'x') || ((s)[1] == 'X'))))

static boolean_t
unhex(const char **h, uchar_t *l, int len)
{
	const char	*hx = *h;
	char	ch;
	uchar_t	byte;

	for (; len--; ) {
		ch = *hx++;
		if (!isxdigit(ch))
			return (B_FALSE);
		if (isdigit(ch))
			byte = ch - '0';
		else
			byte = ch - (isupper(ch) ? 'A' - 10 : 'a' - 10);
		byte <<= 4;
		ch = *hx++;
		if (!isxdigit(ch))
			return (B_FALSE);
		if (isdigit(ch))
			byte |= ch - '0';
		else
			byte |= ch - (isupper(ch) ? 'A' - 10 : 'a' - 10);
		*l++ = byte;
	}
	*h = hx;
	return (B_TRUE);
}

/*
 * Formats accepted:
 * 0x + 4 class + 64 comps + end of string
 * 0x + 4 class + '-' + ll + '-' + comps + end of string
 * ll = number of words to fill out the entire comps field
 *      presumes trailing zero for comps
 *
 * So in the case of 256 comps (i.e., 8 compartment words):
 * 0x0006-08-7ff3f
 * 0x + Classification + Compartments + end of string
 * 0[xX]hhh...
 */

static int
htol(const char *s, m_label_t *l)
{
	const char	*h = &s[2];	/* skip 0[xX] */
	uchar_t *lp = (uchar_t *)&(((_mac_label_impl_t *)l)->_lclass);
	size_t	len = sizeof (_mac_label_impl_t) - 4;
	int	bytes;

	/* unpack 16 bit signed classification */
	if (!unhex(&h, lp, 2) || (LCLASS(l) < 0)) {
		return (-1);
	}
	lp = (uchar_t *)&(((_mac_label_impl_t *)l)->_comps);
	if (h[0] == '-' && h[3] == '-') {
		uchar_t size;

		/* length specified of internal text label */
		h++;	/* skip '-' */
		if (!unhex(&h, &size, 1)) {
			return (-1);
		}
		/* convert size from words to bytes */
		if ((size * sizeof (uint32_t)) > len) {
			/*
			 * internal label greater than will fit in current
			 * binary.
			 */
			return (-1);
		}
		bzero(lp, len);
		h++;	/* skip '-' */
	}
	bytes = strlen(h)/2;
	if ((bytes > len) ||
	    (bytes*2 != strlen(h)) ||
	    !unhex(&h, lp, bytes)) {
		return (-1);
	}
	return (0);
}

/*
 * hexstr_to_label -- parse a string representing a hex label into a
 *			binary label.  Only admin high/low and hex are
 *			accepted.
 *
 *	Returns	 0, success.
 *		-1, failure
 */
int
hexstr_to_label(const char *s, m_label_t *l)
{
	uint_t	f = L_DEFAULT;

	/* translate hex, admin_low and admin_high */
	if (IS_LOW(s)) {
		_LOW_LABEL(l, SUN_MAC_ID);
		return (0);
	} else if (IS_HIGH(s)) {
		_HIGH_LABEL(l, SUN_MAC_ID);
		return (0);
	} else if (IS_HEX(f, s)) {
		_LOW_LABEL(l, SUN_MAC_ID);
		if (htol(s, l) == 0)
			return (0);
	}

	return (-1);
}

static int
convert_id(m_label_type_t t)
{
	switch (t) {
	case MAC_LABEL:
		return (SUN_MAC_ID);
	case USER_CLEAR:
		return (SUN_UCLR_ID);
	default:
		return (-1);
	}
}

/*
 * str_to_label -- parse a string into the requested label type.
 *
 *	Entry	s = string to parse.
 *		l = label to create or modify.
 *		t = label type (MAC_LABEL, USER_CLEAR).
 *		f = flags
 *			L_DEFAULT,
 *			L_MODIFY_EXISTING, use the existing label as a basis for
 *				the parse string.
 *			L_NO_CORRECTION, s must be correct and full by the
 *				label_encoding rules.
 *			L_CHECK_AR, for non-hex s, MAC_LABEL, check the l_e AR
 *
 *	Exit	l = parsed label value.
 *		e = index into string of error.
 *		  = M_BAD_STRING (-3 L_BAD_LABEL) or could be zero,
 *		    indicates entire string,
 *	        e = M_BAD_LABEL (-2 L_BAD_CLASSIFICATION), problems with l
 *		e = M_OUTSIDE_AR (-4 unrelated to L_BAD_* return values)
 *
 *	Returns	 0, success.
 *		-1, failure
 *			errno = ENOTSUP, the underlying label mechanism
 *				does not support label parsing.
 *				ENOMEM, unable to allocate memory for l.
 *				EINVAL, invalid argument, l != NULL or
 *				invalid label type for the underlying
 *				label mechanism.
 */
#define	_M_GOOD_LABEL	-1	/* gfi L_GOOD_LABEL */
int
str_to_label(const char *str, m_label_t **l, const m_label_type_t t, uint_t f,
    int *e)
{
	char		*s = strdup(str);
	char		*st = s;
	char		*p;
	labeld_data_t	call;
	labeld_data_t	*callp = &call;
	size_t		bufsize = sizeof (labeld_data_t);
	size_t		datasize;
	int		err = M_BAD_LABEL;
	int		id = convert_id(t);
	boolean_t	new = B_FALSE;
	uint_t		lf = (f & ~L_CHECK_AR);	/* because L_DEFAULT == 0 */

	if (st == NULL) {
		errno = ENOMEM;
		return (-1);
	}
	if (*l == NULL) {
		if ((*l = m_label_alloc(t)) == NULL) {
			free(st);
			return (-1);
		}
		if (id == -1) {
			goto badlabel;
		}
		_LOW_LABEL(*l, id);
		new = B_TRUE;
	} else if (_MTYPE(*l, SUN_INVALID_ID) &&
	    ((lf == L_NO_CORRECTION) || (lf == L_DEFAULT))) {
		_LOW_LABEL(*l, id);
		new = B_TRUE;
	} else if (!(_MTYPE(*l, SUN_MAC_ID) || _MTYPE(*l, SUN_CLR_ID))) {
		goto badlabel;
	}

	if (new == B_FALSE && id == -1) {
		goto badlabel;
	}

	/* get to the beginning of the string to parse */
	while (isspace(*s)) {
		s++;
	}

	/* accept a leading '[' and trailing ']' for old times sake */
	if (*s == '[') {
		*s = ' ';
		s++;
		while (isspace(*s)) {
			s++;
		}
	}
	p = s;
	while (*p != '\0' && *p != ']') {
		p++;
	}

	/* strip trailing spaces */
	while (p != s && isspace(*(p-1))) {
		--p;
	}
	*p = '\0';	/* end of string */

	/* translate hex, admin_low and admin_high */
	id = _MGETTYPE(*l);
	if (IS_LOW(s)) {
		_LOW_LABEL(*l, id);
		goto goodlabel;
	} else if (IS_HIGH(s)) {
		_HIGH_LABEL(*l, id);
		goto goodlabel;
	} else if (IS_HEX(lf, s)) {
		if (htol(s, *l) != 0) {
			/* whole string in error */
			err = 0;
			goto badlabel;
		}
		goto goodlabel;
	}
#define	slcall callp->param.acall.cargs.sl_arg
#define	slret callp->param.aret.rvals.sl_ret
	/* now try label server */

	datasize = CALL_SIZE_STR(sl_call_t, strlen(st) + 1);
	if (datasize > bufsize) {
		if ((callp = malloc(datasize)) == NULL) {
			free(st);
			return (-1);
		}
		bufsize = datasize;
	}
	callp->callop = STOL;
	slcall.label = **l;
	slcall.flags = f;
	if (new)
		slcall.flags |= L_NEW_LABEL;
	(void) strcpy(slcall.string, st);
	/*
	 * callp->reterr = L_GOOD_LABEL (-1) == OK;
	 *		   L_BAD_CLASSIFICATION (-2) == bad input
	 *			classification: class
	 *		   L_BAD_LABEL (-3) == either string or input label bad
	 *		   M_OUTSIDE_AR (-4) == resultant MAC_LABEL is out
	 *			l_e accreditation range
	 *		   O'E == offset in string 0 == entire string.
	 */
	if (__call_labeld(&callp, &bufsize, &datasize) == SUCCESS) {

		err = callp->reterr;
		if (callp != &call) {
			/* free allocated buffer */
			free(callp);
		}
		switch (err) {
		case _M_GOOD_LABEL:	/* L_GOOD_LABEL */
			**l = slret.label;
			goto goodlabel;
		case M_BAD_LABEL:	/* L_BAD_CLASSIFICATION */
		case M_BAD_STRING:	/* L_BAD_LABEL */
		default:
			goto badlabel;
		}
	}
	switch (callp->reterr) {
	case NOSERVER:
		errno = ENOTSUP;
		break;
	default:
		errno = EINVAL;
		break;
	}
	free(st);
	return (-1);

badlabel:
	errno = EINVAL;
	free(st);
	if (e != NULL)
		*e = err;
	return (-1);

goodlabel:
	free(st);
	return (0);
}
#undef	slcall
#undef	slret

/*
 * m_label_alloc -- allocate a label structure
 *
 *	Entry	t = label type (MAC_LABEL, USER_CLEAR).
 *
 *	Exit	If error, NULL, errno set to ENOMEM
 *		Otherwise, pointer to m_label_t memory
 */

/* ARGUSED */
m_label_t *
m_label_alloc(const m_label_type_t t)
{
	m_label_t *l;

	switch (t) {
	case MAC_LABEL:
	case USER_CLEAR:
		if ((l = malloc(sizeof (_mac_label_impl_t))) == NULL) {
			return (NULL);
		}
		_MSETTYPE(l, SUN_INVALID_ID);
		break;
	default:
		errno = EINVAL;
		return (NULL);
	}
	return (l);
}

/*
 * m_label_dup -- make a duplicate copy of the given label.
 *
 *	Entry	l = label to duplicate.
 *
 *	Exit	d = duplicate copy of l.
 *
 *	Returns	 0, success
 *		-1, if error.
 *			errno = ENOTSUP, the underlying label mechanism
 *				does not support label duplication.
 *				ENOMEM, unable to allocate memory for d.
 *				EINVAL, invalid argument, l == NULL or
 *				invalid label type for the underlying
 *				label mechanism.
 */

int
m_label_dup(m_label_t **d, const m_label_t *l)
{
	if (d == NULL || *d != NULL) {
		errno = EINVAL;
		return (-1);
	}
	if ((*d = malloc(sizeof (_mac_label_impl_t))) == NULL) {
		errno = ENOMEM;
		return (-1);
	}

	(void) memcpy(*d, l, sizeof (_mac_label_impl_t));
	return (0);
}

/*
 * m_label_free -- free label structure
 *
 *	Entry	l = label to free.
 *
 *	Exit	memory freed.
 *
 */

void
m_label_free(m_label_t *l)
{
	if (l)
		free(l);
}
