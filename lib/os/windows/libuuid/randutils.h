#ifndef UTIL_LINUX_RANDUTILS
#define UTIL_LINUX_RANDUTILS

#ifdef HAVE_SRANDOM
#define srand(x)	srandom(x)
#define rand()		random()
#endif

extern int random_get_fd(void);
extern void random_get_bytes(void *buf, size_t nbytes);

#endif
