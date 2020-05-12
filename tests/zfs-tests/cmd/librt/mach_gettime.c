#include <sys/types.h>
#include <sys/_types/_timespec.h>
#include <mach/mach.h>
#include <mach/clock.h>
#include <mach/mach_time.h>

/* As used by test-runner.py */
#define	CLOCK_MONOTONIC_RAW 4

typedef int clockid_t;

extern int clock_gettime(clockid_t clk_id, struct timespec *tp);

int
clock_gettime(clockid_t clk_id, struct timespec *tp)
{
	int retval = 0;
	if (clk_id == CLOCK_MONOTONIC_RAW) {
		clock_serv_t cclock;
		mach_timespec_t mts;

		host_get_clock_service(mach_host_self(), CALENDAR_CLOCK,
		    &cclock);
		retval = clock_get_time(cclock, &mts);
		mach_port_deallocate(mach_task_self(), cclock);

		tp->tv_sec = mts.tv_sec;
		tp->tv_nsec = mts.tv_nsec;
	}
	return (0);
}
