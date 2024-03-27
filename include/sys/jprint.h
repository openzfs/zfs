/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://opensource.org/licenses/CDDL-1.0.
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
 * Copyright (c) 2024, Klara Inc.
 */

#ifndef _SYS_JPRINT_H
#define	_SYS_JPRINT_H

/* maximum stack nesting */
#define	JP_MAX_STACK	32

enum jp_type {
	JP_OBJECT = 1,
	JP_ARRAY
};

struct jp_stack {
	enum jp_type type;
	int nelem;
};

typedef struct jprint {
	char *buffer;		/* pointer to application's buffer */
	size_t buflen;		/* length of buffer */
	char *bufp;		/* current write position in buffer */
	char tmpbuf[32];	/* local buffer for conversions */
	int error;		/* error code */
	int ncall;		/* API call number on which error occurred */
	struct jp_stack		/* stack of array/object nodes */
	    stack[JP_MAX_STACK];
	int stackp;
} jprint_t;

/* error return codes */
#define	JPRINT_OK		0	/* no error */
#define	JPRINT_BUF_FULL		1	/* output buffer full */
#define	JPRINT_NEST_ERROR	2	/* nesting error */
#define	JPRINT_STACK_FULL	3	/* array/object nesting  */
#define	JPRINT_STACK_EMPTY	4	/* stack underflow error */
#define	JPRINT_OPEN		5	/* not all objects closed */
#define	JPRINT_FMT		6	/* format error */

const char *jp_errorstring(int err);
int jp_error(jprint_t *jp);
void jp_open(jprint_t *jp, char *buffer, size_t buflen);
int jp_close(jprint_t *jp);
int jp_errorpos(jprint_t *jp);
int jp_printf(jprint_t *jp, const char *fmt, ...);

#endif	/* _SYS_JPRINT_H */
