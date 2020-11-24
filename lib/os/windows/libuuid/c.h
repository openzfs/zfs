/*
 * Fundamental C definitions.
 */

#ifndef UTIL_LINUX_C_H
#define UTIL_LINUX_C_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <limits.h>
#include <stddef.h>

#ifdef HAVE_STDINT_H
#include <stdint.h>
#else
# ifdef HAVE_INTTYPES_H
# include <inttypes.h>
# endif
#endif

#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef HAVE_ERR_H
# include <err.h>
#endif

#ifndef HAVE_USLEEP
# include <time.h>
#endif

/*
 * Compiler specific stuff
 */
#ifndef __GNUC_PREREQ
# if defined __GNUC__ && defined __GNUC_MINOR__
#  define __GNUC_PREREQ(maj, min) \
	((__GNUC__ << 16) + __GNUC_MINOR__ >= ((maj) << 16) + (min))
# else
#  define __GNUC_PREREQ(maj, min) 0
# endif
#endif

#ifdef __GNUC__

/* &a[0] degrades to a pointer: a different type from an array */
# define __must_be_array(a) \
	UL_BUILD_BUG_ON_ZERO(__builtin_types_compatible_p(__typeof__(a), __typeof__(&a[0])))

# define ignore_result(x) ({ \
	__typeof__(x) __dummy __attribute__((__unused__)) = (x); (void) __dummy; \
})

#else /* !__GNUC__ */
# define __must_be_array(a)	0
# define __attribute__(_arg_)
# define ignore_result(x) ((void) (x))
#endif /* !__GNUC__ */

/*
 * Function attributes
 */
#ifndef __ul_alloc_size
# if __GNUC_PREREQ (4, 3)
#  define __ul_alloc_size(s) __attribute__((alloc_size(s)))
# else
#  define __ul_alloc_size(s)
# endif
#endif

#ifndef __ul_calloc_size
# if __GNUC_PREREQ (4, 3)
#  define __ul_calloc_size(n, s) __attribute__((alloc_size(n, s)))
# else
#  define __ul_calloc_size(n, s)
# endif
#endif

/* Force a compilation error if condition is true, but also produce a
 * result (of value 0 and type size_t), so the expression can be used
 * e.g. in a structure initializer (or where-ever else comma expressions
 * aren't permitted).
 */
#define UL_BUILD_BUG_ON_ZERO(e) (sizeof(struct { int:-!!(e); }))
#define BUILD_BUG_ON_NULL(e) ((void *)sizeof(struct { int:-!!(e); }))

#ifndef ARRAY_SIZE
# define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]) + __must_be_array(arr))
#endif

#ifndef PATH_MAX
# define PATH_MAX 4096
#endif

#ifndef TRUE
# define TRUE 1
#endif

#ifndef FALSE
# define FALSE 0
#endif

#ifndef min
# define min(x, y) ({				\
	__typeof__(x) _min1 = (x);		\
	__typeof__(y) _min2 = (y);		\
	(void) (&_min1 == &_min2);		\
	_min1 < _min2 ? _min1 : _min2; })
#endif

#ifndef max
# define max(x, y) ({				\
	__typeof__(x) _max1 = (x);		\
	__typeof__(y) _max2 = (y);		\
	(void) (&_max1 == &_max2);		\
	_max1 > _max2 ? _max1 : _max2; })
#endif

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#endif

#ifndef container_of
#define container_of(ptr, type, member) ({                       \
	const __typeof__( ((type *)0)->member ) *__mptr = (ptr); \
	(type *)( (char *)__mptr - offsetof(type,member) );})
#endif

#ifndef HAVE_PROGRAM_INVOCATION_SHORT_NAME
# ifdef HAVE___PROGNAME
extern char *__progname;
#  define program_invocation_short_name __progname
# else
#  ifdef HAVE_GETEXECNAME
#   define program_invocation_short_name \
		prog_inv_sh_nm_from_file(getexecname(), 0)
#  else
#   define program_invocation_short_name \
		prog_inv_sh_nm_from_file(__FILE__, 1)
#  endif
static char prog_inv_sh_nm_buf[256];
static inline char *
prog_inv_sh_nm_from_file(char *f, char stripext)
{
	char *t;

	if ((t = strrchr(f, '/')) != NULL)
		t++;
	else
		t = f;

	strncpy(prog_inv_sh_nm_buf, t, sizeof(prog_inv_sh_nm_buf) - 1);
	prog_inv_sh_nm_buf[sizeof(prog_inv_sh_nm_buf) - 1] = '\0';

	if (stripext && (t = strrchr(prog_inv_sh_nm_buf, '.')) != NULL)
		*t = '\0';

	return prog_inv_sh_nm_buf;
}
# endif
#endif


#ifndef HAVE_ERR_H
static inline void
errmsg(char doexit, int excode, char adderr, const char *fmt, ...)
{
	fprintf(stderr, "%s: ", program_invocation_short_name);
	if (fmt != NULL) {
		va_list argp;
		va_start(argp, fmt);
		vfprintf(stderr, fmt, argp);
		va_end(argp);
		if (adderr)
			fprintf(stderr, ": ");
	}
	if (adderr)
		fprintf(stderr, "%m");
	fprintf(stderr, "\n");
	if (doexit)
		exit(excode);
}

#ifndef HAVE_ERR
# define err(E, FMT, ...) errmsg(1, E, 1, FMT)
#endif

#ifndef HAVE_ERRX
# define errx(E, FMT, ...) errmsg(1, E, 0, FMT)
#endif

#ifndef HAVE_WARN
# define warn(FMT, ...) errmsg(0, 0, 1, FMT)
#endif

#ifndef HAVE_WARNX
# define warnx(FMT, ...) errmsg(0, 0, 0, FMT)
#endif
#endif /* !HAVE_ERR_H */


static inline __attribute__((const)) int is_power_of_2(unsigned long num)
{
	return (num != 0 && ((num & (num - 1)) == 0));
}

#ifndef HAVE_LOFF_T
typedef int64_t loff_t;
#endif

#if !defined(HAVE_DIRFD) && (!defined(HAVE_DECL_DIRFD) || HAVE_DECL_DIRFD == 0) && defined(HAVE_DIR_DD_FD)
#include <sys/types.h>
#include <dirent.h>
static inline int dirfd(DIR *d)
{
	return d->dd_fd;
}
#endif

/*
 * Fallback defines for old versions of glibc
 */
#include <fcntl.h>

#ifdef O_CLOEXEC
#define UL_CLOEXECSTR	"e"
#else
#define UL_CLOEXECSTR	""
#endif

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif


#ifndef AI_ADDRCONFIG
#define AI_ADDRCONFIG 0x0020
#endif

#ifndef IUTF8
#define IUTF8 0040000
#endif

/*
 * MAXHOSTNAMELEN replacement
 */
static inline size_t get_hostname_max(void)
{
#if HAVE_DECL__SC_HOST_NAME_MAX
	long len = sysconf(_SC_HOST_NAME_MAX);

	if (0 < len)
		return len;
#endif

#ifdef MAXHOSTNAMELEN
	return MAXHOSTNAMELEN;
#elif HOST_NAME_MAX
	return HOST_NAME_MAX;
#endif
	return 64;
}

#ifndef HAVE_USLEEP
/*
 * This function is marked obsolete in POSIX.1-2001 and removed in
 * POSIX.1-2008. It is replaced with nanosleep().
 */
static inline int usleep(__int64 usec)
{
	HANDLE timer;
	LARGE_INTEGER ft;

	ft.QuadPart = -(10 * usec); // Convert to 100 nanosecond interval, negative value indicates relative time

	timer = CreateWaitableTimer(NULL, TRUE, NULL);
	SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
	WaitForSingleObject(timer, INFINITE);
	CloseHandle(timer);
}
#endif

/*
 * Constant strings for usage() functions. For more info see
 * Documentation/howto-usage-function.txt and disk-utils/delpart.c
 */
#define USAGE_HEADER     _("\nUsage:\n")
#define USAGE_OPTIONS    _("\nOptions:\n")
#define USAGE_SEPARATOR  _("\n")
#define USAGE_HELP       _(" -h, --help     display this help and exit\n")
#define USAGE_VERSION    _(" -V, --version  output version information and exit\n")
#define USAGE_MAN_TAIL(_man)   _("\nFor more details see %s.\n"), _man

#define UTIL_LINUX_VERSION _("%s from %s\n"), program_invocation_short_name, PACKAGE_STRING

/*
 * scanf modifiers for "strings allocation"
 */
#ifdef HAVE_SCANF_MS_MODIFIER
#define UL_SCNsA	"%ms"
#elif defined(HAVE_SCANF_AS_MODIFIER)
#define UL_SCNsA	"%as"
#endif

/*
 * seek stuff
 */
#ifndef SEEK_DATA
# define SEEK_DATA	3
#endif
#ifndef SEEK_HOLE
# define SEEK_HOLE	4
#endif

#endif /* UTIL_LINUX_C_H */
