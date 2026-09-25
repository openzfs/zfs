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

#include <sys/types.h>
#include <sys/kmem.h>
#include <sys/sunddi.h>
#include <sys/cmn_err.h>

#define	_OFF_T_DEFINED
#define	_DEV_T_DEFINED
#define	_STAT_DEFINED
#include <stdarg.h>
#include <stddef.h>
#include <wchar.h>
#include <ntdef.h>  // for UNICODE_STRING

/*
 * Allocate a set of pointers to 'n_items' objects of size 'size'
 * bytes.  Each pointer is initialized to nil.
 *
 * The 'size' and 'n_items' values are stashed in the opaque
 * handle returned to the caller.
 *
 * This implementation interprets 'set of pointers' to mean 'array
 * of pointers' but note that nothing in the interface definition
 * precludes an implementation that uses, for example, a linked list.
 * However there should be a small efficiency gain from using an array
 * at lookup time.
 *
 * NOTE	As an optimization, we make our growable array allocations in
 *	powers of two (bytes), since that's how much kmem_alloc (currently)
 *	gives us anyway.  It should save us some free/realloc's ..
 *
 *	As a further optimization, we make the growable array start out
 *	with MIN_N_ITEMS in it.
 */


int
ddi_soft_state_init(void **state_p, uint32_t size, uint32_t n_items)
{
	struct i_ddi_soft_state *ss;

	if (state_p == NULL || *state_p != NULL || size == 0)
		return (EINVAL);

	ss = kmem_zalloc(sizeof (*ss), KM_SLEEP);
	mutex_init(&ss->lock, NULL, MUTEX_DRIVER, NULL);
	ss->size = size;

	if (n_items < MIN_N_ITEMS)
		ss->n_items = MIN_N_ITEMS;
	else {
		int bitlog;

		if ((bitlog = ddi_fls(n_items)) == ddi_ffs(n_items))
			bitlog--;
		ss->n_items = 1 << bitlog;
	}

	ASSERT(ss->n_items >= n_items);

	ss->array = kmem_zalloc(ss->n_items * sizeof (void *), KM_SLEEP);

	*state_p = ss;

	return (0);
}


/*
 * Allocate a state structure of size 'size' to be associated
 * with item 'item'.
 *
 * In this implementation, the array is extended to
 * allow the requested offset, if needed.
 */
int
ddi_soft_state_zalloc(void *state, int item)
{
	struct i_ddi_soft_state *ss;
	void **array;
	void *new_element;

	if ((ss = state) == NULL || item < 0)
		return (DDI_FAILURE);

	mutex_enter(&ss->lock);
	if (ss->size == 0) {
		mutex_exit(&ss->lock);
		cmn_err(CE_WARN, "ddi_soft_state_zalloc: bad handle");
		return (DDI_FAILURE);
	}

	array = ss->array;	/* NULL if ss->n_items == 0 */
	ASSERT(ss->n_items != 0 && array != NULL);

	/*
	 * refuse to tread on an existing element
	 */
	if (item < ss->n_items && array[item] != NULL) {
		mutex_exit(&ss->lock);
		return (DDI_FAILURE);
	}

	/*
	 * Allocate a new element to plug in
	 */
	new_element = kmem_zalloc(ss->size, KM_SLEEP);

	/*
	 * Check if the array is big enough, if not, grow it.
	 */
	if (item >= ss->n_items) {
		void	**new_array;
		uint32_t	new_n_items;
		struct i_ddi_soft_state *dirty;

		/*
		 * Allocate a new array of the right length, copy
		 * all the old pointers to the new array, then
		 * if it exists at all, put the old array on the
		 * dirty list.
		 *
		 * Note that we can't kmem_free() the old array.
		 *
		 * Why -- well the 'get' operation is 'mutex-free', so we
		 * can't easily catch a suspended thread that is just about
		 * to dereference the array we just grew out of.  So we
		 * cons up a header and put it on a list of 'dirty'
		 * pointer arrays.  (Dirty in the sense that there may
		 * be suspended threads somewhere that are in the middle
		 * of referencing them).  Fortunately, we -can- garbage
		 * collect it all at ddi_soft_state_fini time.
		 */
		new_n_items = ss->n_items;
		while (new_n_items < (1 + item))
			new_n_items <<= 1;	/* double array size .. */

		ASSERT(new_n_items >= (1 + item));	/* sanity check! */

		new_array = kmem_zalloc(new_n_items * sizeof (void *),
		    KM_SLEEP);
		/*
		 * Copy the pointers into the new array
		 */
		memcpy(new_array, array, ss->n_items * sizeof (void *));

		/*
		 * Save the old array on the dirty list
		 */
		dirty = kmem_zalloc(sizeof (*dirty), KM_SLEEP);
		dirty->array = ss->array;
		dirty->n_items = ss->n_items;
		dirty->next = ss->next;
		ss->next = dirty;

		ss->array = (array = new_array);
		ss->n_items = new_n_items;
	}

	ASSERT(array != NULL && item < ss->n_items && array[item] == NULL);

	array[item] = new_element;

	mutex_exit(&ss->lock);
	return (DDI_SUCCESS);
}


/*
 * Fetch a pointer to the allocated soft state structure.
 *
 * This is designed to be cheap.
 *
 * There's an argument that there should be more checking for
 * nil pointers and out of bounds on the array.. but we do a lot
 * of that in the alloc/free routines.
 *
 * An array has the convenience that we don't need to lock read-access
 * to it c.f. a linked list.  However our "expanding array" strategy
 * means that we should hold a readers lock on the i_ddi_soft_state
 * structure.
 *
 * However, from a performance viewpoint, we need to do it without
 * any locks at all -- this also makes it a leaf routine.  The algorithm
 * is 'lock-free' because we only discard the pointer arrays at
 * ddi_soft_state_fini() time.
 */
void *
ddi_get_soft_state(void *state, int item)
{
	struct i_ddi_soft_state *ss = state;

	ASSERT(ss != NULL && item >= 0);

	if (item < ss->n_items && ss->array != NULL)
		return (ss->array[item]);
	return (NULL);
}

/*
 * Free the state structure corresponding to 'item.'   Freeing an
 * element that has either gone or was never allocated is not
 * considered an error.  Note that we free the state structure, but
 * we don't shrink our pointer array, or discard 'dirty' arrays,
 * since even a few pointers don't really waste too much memory.
 *
 * Passing an item number that is out of bounds, or a null pointer will
 * provoke an error message.
 */
void
ddi_soft_state_free(void *state, int item)
{
	struct i_ddi_soft_state *ss;
	void **array;
	void *element;
	static char msg[] = "ddi_soft_state_free:";

	if ((ss = state) == NULL) {
		cmn_err(CE_WARN, "%s null handle",
		    msg);
		return;
	}

	element = NULL;

	mutex_enter(&ss->lock);

	if ((array = ss->array) == NULL || ss->size == 0) {
		cmn_err(CE_WARN, "%s bad handle",
		    msg);
	} else if (item < 0 || item >= ss->n_items) {
		cmn_err(CE_WARN, "%s item %d not in range [0..%u]",
		    msg, item, ss->n_items - 1);
	} else if (array[item] != NULL) {
		element = array[item];
		array[item] = NULL;
	}

	mutex_exit(&ss->lock);

	if (element)
		kmem_free(element, ss->size);
}


/*
 * Free the entire set of pointers, and any
 * soft state structures contained therein.
 *
 * Note that we don't grab the ss->lock mutex, even though
 * we're inspecting the various fields of the data structure.
 *
 * There is an implicit assumption that this routine will
 * never run concurrently with any of the above on this
 * particular state structure i.e. by the time the driver
 * calls this routine, there should be no other threads
 * running in the driver.
 */
void
ddi_soft_state_fini(void **state_p)
{
	struct i_ddi_soft_state *ss, *dirty;
	int item;
	static char msg[] = "ddi_soft_state_fini:";

	if (state_p == NULL || (ss = *state_p) == NULL) {
		return;
	}

	if (ss->size == 0) {
		cmn_err(CE_WARN, "%s bad handle",
		    msg);
		return;
	}

	if (ss->n_items > 0) {
		for (item = 0; item < ss->n_items; item++)
			ddi_soft_state_free(ss, item);
		kmem_free(ss->array, ss->n_items * sizeof (void *));
	}

	/*
	 * Now delete any dirty arrays from previous 'grow' operations
	 */
	for (dirty = ss->next; dirty; dirty = ss->next) {
		ss->next = dirty->next;
		kmem_free(dirty->array, dirty->n_items * sizeof (void *));
		kmem_free(dirty, sizeof (*dirty));
	}

	mutex_destroy(&ss->lock);
	kmem_free(ss, sizeof (*ss));

	*state_p = NULL;
}

int
ddi_create_minor_node(dev_info_t *dip, char *name, int spec_type,
    minor_t minor_num, char *node_type, int flag)
{
	dev_t dev;
	int error = 0;
	char *r, *dup;

	dev = minor_num;
	dip->dev = dev;

	MALLOC(dup, char *, strlen(name)+1, M_TEMP, M_WAITOK);
	if (dup == NULL)
		return (ENOMEM);
	memcpy(dup, name, strlen(name));
	dup[strlen(name)] = '\0';

	for (r = dup;
	    (r = strchr(r, '/'));
	    *r = '_')
		/* empty */;

	dip->devc = NULL;
	dip->devb = NULL;

	FREE(dup, M_TEMP);

	return (error);
}


void
ddi_remove_minor_node(dev_info_t *dip, char *name)
{
	if (dip->devc) {
		dip->devc = NULL;
	}
	if (dip->devb) {
		dip->devb = NULL;
	}
}



int
ddi_strtol(const char *str, char **nptr, int base, long *result)
{
	long val;
	int c;
	int xx;
	int neg = 0;
	long multmin;
	long limit;
	const char **ptr = (const char **)nptr;
	const unsigned char *ustr = (const unsigned char *)str;

	if (ptr != (const char **)0)
		*ptr = (char *)ustr; /* in case no number is formed */
	if (base < 0 || base > MBASE || base == 1) {
		/* base is invalid -- should be a fatal error */
		return (EINVAL);
	}
	if (!isalnum(c = *ustr)) {
		while (isspace(c))
			c = *++ustr;
		switch (c) {
		case '-':
			neg++;
			/* FALLTHROUGH */
		case '+':
			c = *++ustr;
		}
	}
	if (base == 0) {
		if (c != '0')
			base = 10;
		else if (ustr[1] == 'x' || ustr[1] == 'X')
			base = 16;
		else
			base = 8;
	}

	/*
	 * for any base > 10, the digits incrementally following
	 *	9 are assumed to be "abc...z" or "ABC...Z"
	 */
	if (!lisalnum(c) || (xx = DIGIT(c)) >= base) {
		/* no number formed */
		return (EINVAL);
	}
	if (base == 16 && c == '0' && (ustr[1] == 'x' || ustr[1] == 'X') &&
	    isxdigit(ustr[2]))
		c = *(ustr += 2); /* skip over leading "0x" or "0X" */

		/* this code assumes that abs(LONG_MIN) >= abs(LONG_MAX) */
	if (neg)
		limit = LONG_MIN;
	else
		limit = -LONG_MAX;
	multmin = limit / (long)base;
	val = -DIGIT(c);
	for (c = *++ustr; lisalnum(c) && (xx = DIGIT(c)) < base; ) {
		/* accumulate neg avoids surprises near LONG_MAX */
		if (val < multmin)
			goto overflow;
		val *= base;
		if (val < limit + xx)
			goto overflow;
		val -= xx;
		c = *++ustr;
	}
	if (ptr != (const char **)0)
		*ptr = (char *)ustr;
	*result = neg ? val : -val;
	return (0);

overflow:
	for (c = *++ustr; lisalnum(c) && (xx = DIGIT(c)) < base; (c = *++ustr))
		;
	if (ptr != (const char **)0)
		*ptr = (char *)ustr;
	return (ERANGE);
}

char *__cdecl
strpbrk(const char *s, const char *b)
{
	const char *p;

	do {
		for (p = b; *p != '\0' && *p != *s; ++p)
			;
		if (*p != '\0')
			return ((char *)s);
	} while (*s++);
	return (NULL);
}

int
ddi_strtoul(const char *str, char **nptr, int base, unsigned long *result)
{
	*result = (unsigned long)_strtoui64(str, nptr, base);
	if (*result == 0)
		return (EINVAL);
	else if (*result == (unsigned long)ULONG_MAX)
		return (ERANGE);
	return (0);
}

int
ddi_strtoull(const char *str, char **nptr, int base, unsigned long long *result)
{
	*result = (unsigned long long)_strtoui64(str, nptr, base);
	if (*result == 0)
		return (EINVAL);
	else if (*result == ULLONG_MAX)
		return (ERANGE);
	return (0);
}

int
ddi_strtoll(const char *str, char **nptr, int base, long long *result)
{
	long long val;
	int c;
	int xx;
	int neg = 0;
	long long multmin;
	long long limit;
	const char **ptr = (const char **)nptr;
	const unsigned char *ustr = (const unsigned char *)str;

	if (ptr != (const char **)0)
		*ptr = (char *)ustr; /* in case no number is formed */
	if (base < 0 || base > MBASE || base == 1) {
		/* base is invalid -- should be a fatal error */
		return (EINVAL);
	}
	if (!isalnum(c = *ustr)) {
		while (isspace(c))
			c = *++ustr;
		switch (c) {
		case '-':
			neg++;
			/* FALLTHROUGH */
		case '+':
			c = *++ustr;
		}
	}
	if (base == 0) {
		if (c != '0')
			base = 10;
		else if (ustr[1] == 'x' || ustr[1] == 'X')
			base = 16;
		else
			base = 8;
	}
	/*
	 * for any base > 10, the digits incrementally following
	 *	9 are assumed to be "abc...z" or "ABC...Z"
	 */
	if (!lisalnum(c) || (xx = DIGIT(c)) >= base) {
		/* no number formed */
		return (EINVAL);
	}
	if (base == 16 && c == '0' && (ustr[1] == 'x' || ustr[1] == 'X') &&
	    isxdigit(ustr[2]))
		c = *(ustr += 2); /* skip over leading "0x" or "0X" */

		/* this code assumes that abs(LONG_MIN) >= abs(LONG_MAX) */
	if (neg)
		limit = LONGLONG_MIN;
	else
		limit = -LONGLONG_MAX;
	multmin = limit / (long)base;
	val = -DIGIT(c);
	for (c = *++ustr; lisalnum(c) && (xx = DIGIT(c)) < base; ) {
		/* accumulate neg avoids surprises near LONG_MAX */
		if (val < multmin)
			goto overflow;
		val *= base;
		if (val < limit + xx)
			goto overflow;
		val -= xx;
		c = *++ustr;
	}
	if (ptr != (const char **)0)
		*ptr = (char *)ustr;
	*result = neg ? val : -val;
	return (0);

overflow:
	for (c = *++ustr; lisalnum(c) && (xx = DIGIT(c)) < base; (c = *++ustr))
		;
	if (ptr != (const char **)0)
		*ptr = (char *)ustr;
	return (ERANGE);
}

uint32_t
ddi_strcspn(const char *__restrict s, const char *__restrict charset)
{
	/*
	 * NB: idx and bit are temporaries whose use causes gcc 3.4.2 to
	 * generate better code.  Without them, gcc gets a little confused.
	 */
	const char *s1;
	ulong_t bit;
	ulong_t tbl[(255 + 1) / LONG_BIT];
	int idx;
	if (*s == '\0')
		return (0);

	// 64bit code
	tbl[0] = 1;
	tbl[3] = tbl[2] = tbl[1] = 0;
	for (; *charset != '\0'; charset++) {
		idx = IDX(*charset);
		bit = BIT(*charset);
		tbl[idx] |= bit;

	}

	for (s1 = s; ; s1++) {
		idx = IDX(*s1);
		bit = BIT(*s1);
		if ((tbl[idx] & bit) != 0)
			break;
		}
	return (uint32_t)(s1 - s);
}

extern size_t
strlcpy(char *s, const char *t, size_t n)
{
	const char *o = t;

	if (n)
		do {
			if (!--n) {
				*s = 0;
				break;
			}
		} while ((*s++ = *t++) != 0);
	if (!n)
		while (*t++)
			;
	return (uint32_t)(t - o - 1);
}

extern size_t
strlcat(char *s, const char *t, size_t n)
{
	register size_t m;
	const char *o = t;

	if ((m = n) != 0) {
		while (n && *s) {
			n--;
			s++;
		}
		m -= n;
		if (n)
			do {
				if (!--n) {
					*s = 0;
					break;
				}
			} while ((*s++ = *t++) != 0);
		else
			*s = 0;
	}
	if (!n)
		while (*t++)
			;
	return ((t - o) + m - 1);
}

/*
 * We can't use the standard vsnprintf() function
 * as it lives in CRT and is not available in the kernel.
 * (We can get away with that on Intel, but not on ARM64.)
 * So we implement a simple version of it here, based on
 * the FreeBSD implementation.
 * We can not use the Windows RtlStringCbVPrintfExA()
 * as OpenZFS uses length = snprintf(NULL, 0, ...) which
 * Windows API does not support.
 */

struct vsnprintf_ctx {
	char *buf;
	size_t size;
	size_t len;
};

static void
putc_vsn(struct vsnprintf_ctx *ctx, char c)
{
	if (ctx->len + 1 < ctx->size)
		ctx->buf[ctx->len] = c;
	ctx->len++;
}

static void
puts_vsn(struct vsnprintf_ctx *ctx, const char *s)
{
	if (!s) {
		puts_vsn(ctx, "(null)");
		return;
	}

	while (*s)
		putc_vsn(ctx, *s++);
}

static void
putnum_vsn(struct vsnprintf_ctx *ctx, unsigned long long num, int base,
    int is_signed, int is_upper, int width, int zero_pad)
{
	char buf[32];
	const char *digits = is_upper ? "0123456789ABCDEF" : "0123456789abcdef";
	int i = 0;

	if (num == 0) {
		buf[i++] = '0';
	} else {
		while (num &&i < (int)sizeof (buf))
			buf[i++] = digits[num % base], num /= base;
	}

	int pad = width > i ? width - i : 0;
	while (pad-- > 0)
		putc_vsn(ctx, zero_pad ? '0' : ' ');

	while (i--)
		putc_vsn(ctx, buf[i]);
}

int
vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
	struct vsnprintf_ctx ctx = { buf, size, 0 };

	while (*fmt) {
		if (*fmt != '%') {
			putc_vsn(&ctx, *fmt++);
			continue;
		}

		fmt++;  // skip '%'

		// --- Parse flags ---
		int zero_pad = 0;
		while (*fmt == '0') {
			zero_pad = 1;
			fmt++;
		}

		// --- Parse width ---
		int width = 0;

		if (*fmt == '*') {
			fmt++;
			width = va_arg(ap, int);
		} else {
			while (*fmt >= '0' && *fmt <= '9') {
				width = width * 10 + (*fmt - '0');
				fmt++;
			}
		}

		// Parse precision: .<num> or .*
		int precision = -1;  // -1 = not set
		if (*fmt == '.') {
			fmt++;
			if (*fmt == '*') {
				fmt++;
				precision = va_arg(ap, int);
			} else {
				precision = 0;
				while (*fmt >= '0' && *fmt <= '9')
					precision =
					    precision * 10 + (*fmt++ - '0');
			}
		}

		// --- Parse length modifier ---
		int is_long = 0, is_longlong = 0, is_size_t = 0, is_intmax = 0;
		int is_I64 = 0;

		if (*fmt == 'I') {
			fmt++;
			if (fmt[0] == '6' && fmt[1] == '4') {
				is_I64 = 1;
				fmt += 2;
			} else {
				/*
				 * Windows %Iu / %Id / %Ix often means
				 * pointer-sized integer. On 64-bit
				 * Windows that is 64-bit, on 32-bit
				 * it is 32-bit.
				 */
				is_size_t = 1;
			}
		}

		while (*fmt == 'l' || *fmt == 'z') {
			if (*fmt == 'l') {
				if (is_long) is_longlong = 1;
				is_long = 1;
			} else if (*fmt == 'z') {
				is_size_t = 1;
			}
			fmt++;
		}
		if (*fmt == 'L') {
			is_longlong = 1;
			fmt++;
		}
		if (*fmt == 'j') {
			is_intmax = 1;
			fmt++;
		}

		// --- Format specifier ---
		char spec = *fmt++;
		switch (spec) {
		case 's': {
			const char *s = va_arg(ap, const char *);
			if (!s) s = "(null)";

			int len = 0;
			while ((precision < 0 || len < precision) && s[len])
				len++;

			int pad = width > len ? width - len : 0;
			while (pad-- > 0)
				putc_vsn(&ctx, zero_pad ? '0' : ' ');

			for (int i = 0; i < len; i++)
				putc_vsn(&ctx, s[i]);
			break;
		}
		case 'S': {
			const WCHAR *ws = va_arg(ap, const WCHAR *);
			if (!ws) ws = L"(null)";

			int len = 0;
			while ((precision < 0 || len < precision) && ws[len])
				len++;

			int pad = width > len ? width - len : 0;
			while (pad-- > 0)
				putc_vsn(&ctx, zero_pad ? '0' : ' ');

			for (int i = 0; i < len; i++)
				putc_vsn(&ctx, (char)(ws[i] & 0x7F));
			break;
		}
		case 'w': {
			if (*fmt == 'Z') {
				fmt++;
				void *arg = va_arg(ap, void *);

				if (arg == NULL) {
					puts_vsn(&ctx, "(null)");
					break;
				}

				UNICODE_STRING *us = (UNICODE_STRING *)arg;

				if (us && us->Buffer) {
					for (USHORT i = 0;
					    i < us->Length / sizeof (WCHAR);
					    i++)
						putc_vsn(&ctx, (char)
						    (us->Buffer[i] & 0x7F));
				}
			}
			break;
		}
		case 'c': {
			int c = va_arg(ap, int);
			putc_vsn(&ctx, (char)c);
			break;
		}
		case 'd':
		case 'i': {
			long long val;
			if (is_intmax)
				val = va_arg(ap, long long);
			else if (is_I64 || is_longlong)
				val = va_arg(ap, long long);
			else if (is_long)
				val = va_arg(ap, long);
			else
				val = va_arg(ap, int);

			if (val < 0) {
				putc_vsn(&ctx, '-');
				val = -val;
			}

			putnum_vsn(&ctx, (unsigned long long)val, 10, 1,
			    0, width, zero_pad);
			break;
		}
		case 'u':
		case 'x':
		case 'X': {
			unsigned long long val;
			if (is_intmax)
				val = va_arg(ap, unsigned long long);
			else if (is_longlong)
				val = va_arg(ap, unsigned long long);
			else if (is_long)
				val = va_arg(ap, unsigned long);
			else if (is_size_t)
				val = va_arg(ap, size_t);
			else
				val = va_arg(ap, unsigned int);

			int base = (spec == 'u') ? 10 : 16;
			int upper = (spec == 'X') ? 1 : 0;
			putnum_vsn(&ctx, val, base, 0, upper, width, zero_pad);
			break;
		}
		case 'p': {
			uintptr_t ptr = (uintptr_t)va_arg(ap, void *);
			puts_vsn(&ctx, "0x");
			putnum_vsn(&ctx, ptr, 16, 0, 0,
			    sizeof (uintptr_t) * 2, 1);
			break;
		}
		case '%': {
			putc_vsn(&ctx, '%');
			break;
		}
		/* Then any fmt we dont support, but must consume va_arg for */
		case 'n': {
			int *p = va_arg(ap, int *);
			if (p) *p = (int)ctx.len;
			break;
		}
		case 'f':
		case 'F':
		case 'e':
		case 'E':
		case 'g':
		case 'G':
		case 'a':
		case 'A':
			if (is_longlong)
				(void) va_arg(ap, long double);
			else
				(void) va_arg(ap, double);
			break;

		default:
			putc_vsn(&ctx, '%');
			putc_vsn(&ctx, spec);
			break;
		}
	}

	if (ctx.size > 0)
		ctx.buf[ctx.len < ctx.size ? ctx.len : ctx.size - 1] = '\0';

	return ((int)ctx.len);
}
