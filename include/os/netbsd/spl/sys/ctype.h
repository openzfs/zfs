// SPDX-License-Identifier: CDDL-1.0
#ifndef _SPL_SYS_CTYPE_H_
#define	_SPL_SYS_CTYPE_H_

#ifndef isdigit
#define	isdigit(c)	((unsigned)(c) - '0' < 10)
#endif
#ifndef isalpha
#define	isalpha(c)	(((unsigned)(c) | 32) - 'a' < 26)
#endif
#ifndef isalnum
#define	isalnum(ch)	(isalpha(ch) || isdigit(ch))
#endif
#ifndef iscntrl
#define	iscntrl(C)	\
	((unsigned char)(C) <= 0x1f || (unsigned char)(C) == 0x7f)
#endif
#ifndef isgraph
#define	isgraph(C)	((C) >= 0x21 && (C) <= 0x7E)
#endif
#ifndef ispunct
#define	ispunct(C)							\
	(((C) >= 0x21 && (C) <= 0x2F) ||				\
		((C) >= 0x3A && (C) <= 0x40) ||				\
		((C) >= 0x5B && (C) <= 0x60) ||				\
		((C) >= 0x7B && (C) <= 0x7E))
#endif
#ifndef isspace
#define	isspace(C)	((C) == 0x20 || ((C) >= 0x9 && (C) <= 0xD))
#endif
#ifndef isprint
#define	isprint(C)	\
	((unsigned char)(C) >= 0x20 && (unsigned char)(C) <= 0x7e)
#endif
#ifndef isxdigit
#define	isxdigit(c)	(isdigit(c) || \
	(((unsigned)(c) | 32) - 'a' < 6))
#endif

#endif
