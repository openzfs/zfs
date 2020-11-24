/*
 * General purpose random utilities
 *
 * Based on libuuid code.
 *
 * This file may be redistributed under the terms of the
 * GNU Lesser General Public License.
 */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

//#include <sys/syscall.h>

#include "randutils.h"

#ifdef HAVE_TLS
#define THREAD_LOCAL static __thread
#else
#define THREAD_LOCAL static
#endif

#if defined(__linux__) && defined(__NR_gettid) && defined(HAVE_JRAND48)
#define DO_JRAND_MIX
THREAD_LOCAL unsigned short ul_jrand_seed[3];
#endif

#ifndef _WIN32
int random_get_fd(void)
{
	int i, fd;
	struct timeval	tv;

	gettimeofday(&tv, 0);
	fd = open("/dev/urandom", O_RDONLY);
	if (fd == -1)
		fd = open("/dev/random", O_RDONLY | O_NONBLOCK);
	if (fd >= 0) {
		i = fcntl(fd, F_GETFD);
		if (i >= 0)
			fcntl(fd, F_SETFD, i | FD_CLOEXEC);
	}
	srand((getpid() << 16) ^ getuid() ^ tv.tv_sec ^ tv.tv_usec);

#ifdef DO_JRAND_MIX
	ul_jrand_seed[0] = getpid() ^ (tv.tv_sec & 0xFFFF);
	ul_jrand_seed[1] = getppid() ^ (tv.tv_usec & 0xFFFF);
	ul_jrand_seed[2] = (tv.tv_sec ^ tv.tv_usec) >> 16;
#endif
	/* Crank the random number generator a few times */
	gettimeofday(&tv, 0);
	for (i = (tv.tv_sec ^ tv.tv_usec) & 0x1F; i > 0; i--)
		rand();
	return fd;
}
#endif

/*
 * Generate a stream of random nbytes into buf.
 * Use /dev/urandom if possible, and if not,
 * use glibc pseudo-random functions.
 */
#ifndef _WIN32
void random_get_bytes(void *buf, size_t nbytes)
{
	size_t i, n = nbytes;
	int fd = random_get_fd();
	int lose_counter = 0;
	unsigned char *cp = (unsigned char *) buf;

	if (fd >= 0) {
		while (n > 0) {
			ssize_t x = read(fd, cp, n);
			if (x <= 0) {
				if (lose_counter++ > 16)
					break;
				continue;
			}
			n -= x;
			cp += x;
			lose_counter = 0;
		}

		close(fd);
	}
	/*
	 * We do this all the time, but this is the only source of
	 * randomness if /dev/random/urandom is out to lunch.
	 */
	for (cp = buf, i = 0; i < nbytes; i++)
		*cp++ ^= (rand() >> 7) & 0xFF;

#ifdef DO_JRAND_MIX
	{
		unsigned short tmp_seed[3];

		memcpy(tmp_seed, ul_jrand_seed, sizeof(tmp_seed));
		ul_jrand_seed[2] = ul_jrand_seed[2] ^ syscall(__NR_gettid);
		for (cp = buf, i = 0; i < nbytes; i++)
			*cp++ ^= (jrand48(tmp_seed) >> 7) & 0xFF;
		memcpy(ul_jrand_seed, tmp_seed,
		       sizeof(ul_jrand_seed)-sizeof(unsigned short));
	}
#endif

	return;
}
#endif

#ifdef TEST_PROGRAM
int main(int argc __attribute__ ((__unused__)),
         char *argv[] __attribute__ ((__unused__)))
{
	unsigned int v, i;

	/* generate and print 10 random numbers */
	for (i = 0; i < 10; i++) {
		random_get_bytes(&v, sizeof(v));
		printf("%d\n", v);
	}

	return EXIT_SUCCESS;
}
#endif /* TEST_PROGRAM */
