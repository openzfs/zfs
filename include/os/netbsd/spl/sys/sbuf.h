// SPDX-License-Identifier: BSD-2-Clause
#ifndef	_OPENSOLARIS_SYS_SBUF_H_
#define	_OPENSOLARIS_SYS_SBUF_H_

#ifdef _KERNEL
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kmem.h>
#include <sys/errno.h>
#include <sys/stdint.h>
#include <sys/sysctl.h>

struct sbuf {
	char	*s_buf;
	size_t	s_size;
	size_t	s_len;
	int	s_error;
	int	s_flags;
	unsigned s_ownbuf:1;
	unsigned s_ownstruct:1;
	struct sysctl_req *s_req;
};

#define	SBUF_AUTOEXTEND	0x1
#define	SBUF_FIXEDLEN	0x0

static inline int
sbuf_grow(struct sbuf *s, size_t need)
{
	char *nbuf;
	size_t nsize;

	if (s->s_error != 0)
		return (s->s_error);
	if ((s->s_flags & SBUF_AUTOEXTEND) == 0) {
		s->s_error = ENOMEM;
		return (ENOMEM);
	}
	nsize = (s->s_size > 0) ? s->s_size : 256;
	while (nsize < need) {
		if (nsize > SIZE_MAX / 2) {
			s->s_error = ENOMEM;
			return (ENOMEM);
		}
		nsize *= 2;
	}
	nbuf = kmem_zalloc(nsize, KM_SLEEP);
	if (nbuf == NULL) {
		s->s_error = ENOMEM;
		return (ENOMEM);
	}
	if (s->s_buf != NULL && s->s_len > 0)
		memcpy(nbuf, s->s_buf, s->s_len);
	if (s->s_ownbuf && s->s_buf != NULL)
		kmem_free(s->s_buf, s->s_size);
	s->s_buf = nbuf;
	s->s_size = nsize;
	s->s_ownbuf = 1;
	return (0);
}

static inline struct sbuf *
sbuf_new(struct sbuf *s, char *buf, int size, int flags)
{
	int ownstruct = 0;

	if (s == NULL) {
		s = kmem_zalloc(sizeof (*s), KM_SLEEP);
		ownstruct = 1;
	} else {
		memset(s, 0, sizeof (*s));
	}
	s->s_flags = flags;
	s->s_ownstruct = ownstruct;
	if (buf == NULL) {
		size_t n = (size <= 0) ? 256 : (size_t)size;

		s->s_buf = kmem_zalloc(n, KM_SLEEP);
		if (s->s_buf == NULL) {
			if (ownstruct)
				kmem_free(s, sizeof (*s));
			return (NULL);
		}
		s->s_size = n;
		s->s_ownbuf = 1;
	} else {
		s->s_buf = buf;
		s->s_size = (size > 0) ? (size_t)size : 0;
		s->s_ownbuf = 0;
	}
	s->s_len = 0;
	s->s_error = 0;
	return (s);
}

static inline int
sbuf_printf(struct sbuf *s, const char *fmt, ...)
{
	va_list ap;
	int n;
	size_t room;

	if (s->s_error != 0)
		return (-1);
	for (;;) {
		room = (s->s_size > s->s_len) ? s->s_size - s->s_len : 0;
		if (room == 0) {
			if (s->s_size > SIZE_MAX - 64) {
				s->s_error = ENOMEM;
				return (-1);
			}
			if (sbuf_grow(s, s->s_size + 64) != 0)
				return (-1);
			continue;
		}
		va_start(ap, fmt);
		n = vsnprintf(s->s_buf + s->s_len, room, fmt, ap);
		va_end(ap);
		if (n < 0) {
			s->s_error = EINVAL;
			return (-1);
		}
		if ((size_t)n < room) {
			s->s_len += (size_t)n;
			return (0);
		}
		if ((size_t)n > SIZE_MAX - s->s_len - 1) {
			s->s_error = ENOMEM;
			return (-1);
		}
		if (sbuf_grow(s, s->s_len + (size_t)n + 1) != 0)
			return (-1);
	}
}

static inline int
sbuf_cat(struct sbuf *s, const char *str)
{
	return (sbuf_printf(s, "%s", str));
}

static inline int
sbuf_putc(struct sbuf *s, int c)
{
	return (sbuf_printf(s, "%c", c));
}

static inline int
sbuf_finish(struct sbuf *s)
{
	if (s->s_buf != NULL && s->s_len < s->s_size)
		s->s_buf[s->s_len] = '\0';
	if (s->s_req != NULL && s->s_req->oldptr != NULL && s->s_buf != NULL) {
		size_t n = s->s_len + 1;

		if (s->s_req->oldlen > 0 && n > s->s_req->oldlen)
			n = s->s_req->oldlen;
		if (n > 0)
			memcpy(s->s_req->oldptr, s->s_buf, n);
		s->s_req->oldidx = n;
	}
	return (s->s_error);
}

static inline char *
sbuf_data(struct sbuf *s)
{
	return (s->s_buf);
}

static inline int
sbuf_len(struct sbuf *s)
{
	if (s->s_len > (size_t)INT_MAX)
		return (INT_MAX);
	return ((int)s->s_len);
}

static inline void
sbuf_delete(struct sbuf *s)
{
	if (s == NULL)
		return;
	if (s->s_ownbuf && s->s_buf != NULL)
		kmem_free(s->s_buf, s->s_size);
	if (s->s_ownstruct)
		kmem_free(s, sizeof (*s));
}

#define	sbuf_new_auto()	sbuf_new(NULL, NULL, 0, SBUF_AUTOEXTEND)
#define	sbuf_clear(s)	do { (s)->s_len = 0; (s)->s_error = 0; } while (0)

static inline struct sbuf *
sbuf_new_for_sysctl(struct sbuf *s, char *buf, int size, struct sysctl_req *req)
{
	s = sbuf_new(s, buf, size, SBUF_AUTOEXTEND);
	if (s != NULL)
		s->s_req = req;
	return (s);
}
#endif
#endif
