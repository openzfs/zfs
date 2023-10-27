/*
 * This relies on lib/libspl/include/os/macos/sys/time.h
 * being included.
 * Old macOS did not have clock_gettime, and current
 * has it in libc. Linux assumes that we are to use
 * librt for it, so we create this dummy library.
 * The linker will sort out connecting it for us.
 */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/_types/_timespec.h>
#include <mach/mach.h>
#include <mach/clock.h>
#include <mach/mach_time.h>

#if !defined(MAC_OS_X_VERSION_10_12) || \
	(MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_12)
extern int
clock_gettime(clock_id_t clock_id, struct timespec *tp);
#endif

extern void gettime_dummy(void);

void
gettime_dummy(void)
{
	clock_gettime(0, NULL);
}
