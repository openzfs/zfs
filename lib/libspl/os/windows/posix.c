/*
* CDDL HEADER START
*
* The contents of this file are subject to the terms of the
* Common Development and Distribution License, Version 1.0 only
* (the "License").  You may not use this file except in compliance
* with the License.
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
* Copyright (c) 2017 Jorgen Lundman <lundman@lundman.net.  All rights reserved.
*/
#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64
#include <WinSock2.h>
#include <sys/types.h>
#include <sys/types32.h>
#include <time.h>
#include <io.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/mntent.h>
#include <sys/mount.h>
#include <fcntl.h>
#include <sys/zfs_ioctl.h>
#include <pthread.h>
#include <Windows.h>
#include <langinfo.h>

#pragma comment(lib, "ws2_32.lib")

void clock_gettime(clock_type_t t, struct timespec *ts)
{
	LARGE_INTEGER time;
	LARGE_INTEGER frequency;
	FILETIME ft;
	ULONGLONG tmp;

	switch (t) {
	case CLOCK_MONOTONIC:
		QueryPerformanceCounter(&time);
		QueryPerformanceFrequency(&frequency);
		ts->tv_sec = time.QuadPart / frequency.QuadPart;
		ts->tv_nsec = 100*(long)(time.QuadPart % frequency.QuadPart);
		break;

	case CLOCK_REALTIME:
		GetSystemTimeAsFileTime(&ft);
		time.LowPart = ft.dwLowDateTime;
		time.HighPart = ft.dwHighDateTime;
		time.QuadPart -= 116444736000000000; 
		ts->tv_sec = (long)(time.QuadPart / 10000000UL);
		ts->tv_nsec = 100*(long)(time.QuadPart % 10000000UL);
		break;
	default:
		ASSERT(0);
	}
}

void
gethrestime(inode_timespec_t *ts)
{
	struct timeval tv;
	(void) gettimeofday(&tv, NULL);
	ts->tv_sec = tv.tv_sec;
	ts->tv_nsec = tv.tv_usec * NSEC_PER_USEC;
}

uint64_t
gethrestime_sec(void)
{
	struct timeval tv;
	(void) gettimeofday(&tv, NULL);
	return (tv.tv_sec);
}

hrtime_t
gethrtime(void)
{
	struct timespec ts;
	(void) clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((((uint64_t)ts.tv_sec) * NANOSEC) + ts.tv_nsec);
}

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
	void *ptr;
	ptr = _aligned_malloc(size, alignment);
	if (ptr == NULL)
		return ENOMEM;
	*memptr = ptr;
	return 0;
}

const char *getexecname(void)
{
	__declspec(thread) static char execname[32767 + 1];
	GetModuleFileNameA(NULL, execname, sizeof(execname));
	return execname;
}

struct passwd *getpwnam(const char *login)
{
	return NULL;
}

struct passwd *getgrnam(const char *group)
{
	return NULL;
}

struct tm *localtime_r(const time_t *clock, struct tm *result)
{
	if (localtime_s(result, clock) == 0)
		return result;
	// To avoid the ASSERT and abort(), make tm be something valid
	memset(result, 0, sizeof(*result));
	result->tm_mday = 1;
	return NULL;
}

char *
strsep(char **stringp, const char *delim) 
{
	char *s;
	const char *spanp;
	int c, sc;
	char *tok;

	if ((s = *stringp) == NULL)
		return (NULL);
	for (tok = s;;) {
		c = *s++;
		spanp = delim;
		do {
			if ((sc = *spanp++) == c) {
				if (c == 0)
					s = NULL;
				else
					s[-1] = 0;
				*stringp = s;
				return (tok);
			}
		} while (sc != 0);
	}
	/* NOTREACHED */
}

char *realpath(const char *file_name, char *resolved_name)
{
	DWORD ret;
	// If resolved_name is NULL, we allocate space. Otherwise we assume
	// PATH_MAX - but pretty sure this style isn't used in ZFS
	if (resolved_name == NULL)
		resolved_name = malloc(PATH_MAX);
	if (resolved_name == NULL)
		return NULL;
	ret = GetFullPathName(file_name, PATH_MAX, resolved_name, NULL);
	if (ret == 0)
		return NULL;

	return resolved_name;
}

int statfs(const char *path, struct statfs *buf)
{
	ULARGE_INTEGER lpFreeBytesAvailable;
	ULARGE_INTEGER lpTotalNumberOfBytes;
	ULARGE_INTEGER lpTotalNumberOfFreeBytes;
	uint64_t lbsize;

#if 1
	if (GetDiskFreeSpaceEx(path,
		&lpFreeBytesAvailable,
		&lpTotalNumberOfBytes,
		&lpTotalNumberOfFreeBytes))
		return -1;
#endif

	DISK_GEOMETRY_EX geometry_ex;
	HANDLE handle;
	DWORD len;

	int fd = open(path, O_RDONLY | O_BINARY);
	handle = (HANDLE) _get_osfhandle(fd);
	if (!DeviceIoControl(handle, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0,
		&geometry_ex, sizeof(geometry_ex), &len, NULL))
		return -1;
	close(fd);
	lbsize = (uint_t)geometry_ex.Geometry.BytesPerSector;

	buf->f_bsize = lbsize;
	buf->f_blocks = lpTotalNumberOfBytes.QuadPart / lbsize;
	buf->f_bfree = lpTotalNumberOfFreeBytes.QuadPart / lbsize;
	buf->f_bavail = lpTotalNumberOfFreeBytes.QuadPart / lbsize;
	buf->f_type = 0;
	strcpy(buf->f_fstypename, "fixme");
	strcpy(buf->f_mntonname, "fixme_to");
	strcpy(buf->f_mntfromname, "fixme_from");

	return 0;
}


static const char letters[] =
"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
int
mkstemp(char *tmpl)
{
	int len;
	char *XXXXXX;
	static unsigned long long value;
	unsigned long long random_time_bits;
	unsigned int count;
	int fd = -1;
	int save_errno = errno;

#define ATTEMPTS_MIN (62 * 62 * 62)

#if ATTEMPTS_MIN < TMP_MAX
	unsigned int attempts = TMP_MAX;
#else
	unsigned int attempts = ATTEMPTS_MIN;
#endif

	len = strlen(tmpl);
	if (len < 6 || strcmp(&tmpl[len - 6], "XXXXXX"))
	{
		errno = EINVAL;
		return -1;
	}

	XXXXXX = &tmpl[len - 6];

	{
		SYSTEMTIME      stNow;
		FILETIME ftNow;

		// get system time
		GetSystemTime(&stNow);
		stNow.wMilliseconds = 500;
		if (!SystemTimeToFileTime(&stNow, &ftNow))
		{
			errno = -1;
			return -1;
		}

		random_time_bits = (((unsigned long long)ftNow.dwHighDateTime << 32)
			| (unsigned long long)ftNow.dwLowDateTime);
	}
	value += random_time_bits ^ (unsigned long long)GetCurrentThreadId();

	for (count = 0; count < attempts; value += 7777, ++count)
	{
		unsigned long long v = value;

		/* Fill in the random bits.  */
		XXXXXX[0] = letters[v % 62];
		v /= 62;
		XXXXXX[1] = letters[v % 62];
		v /= 62;
		XXXXXX[2] = letters[v % 62];
		v /= 62;
		XXXXXX[3] = letters[v % 62];
		v /= 62;
		XXXXXX[4] = letters[v % 62];
		v /= 62;
		XXXXXX[5] = letters[v % 62];

		fd = open(tmpl, O_RDWR | O_CREAT | O_EXCL, _S_IREAD | _S_IWRITE);
		if (fd >= 0)
		{
			errno = save_errno;
			return fd;
		}
		else if (errno != EEXIST)
			return -1;
	}

	/* We got out of the loop because we ran out of combinations to try.  */
	errno = EEXIST;
	return -1;
}

int readlink(const char *path, char *buf, size_t bufsize)
{
	return -1;
}

int usleep(__int64 usec)
{
	HANDLE timer;
	LARGE_INTEGER ft;

	ft.QuadPart = -(10 * usec); // Convert to 100 nanosecond interval, negative value indicates relative time

	timer = CreateWaitableTimer(NULL, TRUE, NULL);
	SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
	WaitForSingleObject(timer, INFINITE);
	CloseHandle(timer);
	return 0;
}

int
nanosleep(const struct timespec *rqtp, struct timespec *rmtp)
{
	/* Declarations */
	HANDLE timer;	/* Timer handle */
	LARGE_INTEGER li;	/* Time defintion */
						/* Create timer */

	// Negative means relative time, 100 nanosecs on Windows.
	li.QuadPart = -((SEC2NSEC(rqtp->tv_sec) + rqtp->tv_nsec) / 100ULL);

	if (!(timer = CreateWaitableTimer(NULL, TRUE, NULL)))
		return FALSE;

	/* Set timer properties */
	if (!SetWaitableTimer(timer, &li, 0, NULL, NULL, FALSE)) {
		CloseHandle(timer);
		return FALSE;
	}
	/* Start & wait for timer */
	WaitForSingleObject(timer, INFINITE);
	/* Clean resources */
	CloseHandle(timer);
	/* Slept without problems */
	return 0;
}

int strncasecmp(char *s1, char *s2, size_t n)
{
	if (n == 0)
		return 0;

	while (n-- != 0 && tolower(*s1) == tolower(*s2))
	{
		if (n == 0 || *s1 == '\0' || *s2 == '\0')
			break;
		s1++;
		s2++;
	}

	return tolower(*(unsigned char *)s1) - tolower(*(unsigned char *)s2);
}

#define DIRNAME         0
#define BASENAME        1

#define M_FSDELIM(c)    ((c)=='/'||(c)=='\\')
#define M_DRDELIM(c)    (0)

static char curdir[] = ".";
static char *
basedir(char *arg, int type)
{
	register char *cp, *path;

	if (arg == (char *)0 || *arg == '\0' ||
		(*arg == '.' && (arg[1] == '\0' ||
		(type == DIRNAME && arg[1] == '.' && arg[2] == '\0'))))

		return curdir;  /* arg NULL, empty, ".", or ".." in DIRNAME */

	if (M_DRDELIM(arg[1]))  /* drive-specified pathnames */
		path = arg + 2;
	else
		path = arg;

	if (path[1] == '\0'&&M_FSDELIM(*path))    /* "/", or drive analog */
		return arg;

	cp = strchr(path, '\0');
	cp--;

	while (cp != path && M_FSDELIM(*cp))
		*(cp--) = '\0';

	for (;cp>path && !M_FSDELIM(*cp); cp--)
		;

	if (!M_FSDELIM(*cp))
		if (type == DIRNAME && path != arg) {
			*path = '\0';
			return arg;     /* curdir on the specified drive */
		}
		else
			return (type == DIRNAME) ? curdir : path;
	else if (cp == path && type == DIRNAME) {
		cp[1] = '\0';
		return arg;             /* root directory involved */
	}
	else if (cp == path && cp[1] == '\0')
		return(arg);
	else if (type == BASENAME)
		return ++cp;
	*cp = '\0';
	return arg;
}

char *
dirname(char *arg)
{
	return(basedir(arg, DIRNAME));
}

char *
basename(char *arg)
{
	return(basedir(arg, BASENAME));
}

char* getIoctlAsString(int cmdNo) 
{
	switch (cmdNo) {
		case 0x800: return "ZFS_IOC_FIRST";
		case 0x801: return "ZFS_IOC_POOL_DESTROY";
		case 0x802: return "ZFS_IOC_POOL_IMPORT";
		case 0x803: return "ZFS_IOC_POOL_EXPORT";
		case 0x804: return "ZFS_IOC_POOL_CONFIGS";
		case 0x805: return "ZFS_IOC_POOL_STATS";
		case 0x806: return "ZFS_IOC_POOL_TRYIMPORT";
		case 0x807: return "ZFS_IOC_POOL_SCAN";
		case 0x808: return "ZFS_IOC_POOL_FREEZE";
		case 0x809: return "ZFS_IOC_POOL_UPGRADE";
		case 0x80a: return "ZFS_IOC_POOL_GET_HISTORY";
		case 0x80b: return "ZFS_IOC_VDEV_ADD";
		case 0x80c: return "ZFS_IOC_VDEV_REMOVE";
		case 0x80d: return "ZFS_IOC_VDEV_SET_STATE";
		case 0x80e: return "ZFS_IOC_VDEV_ATTACH";
		case 0x80f: return "ZFS_IOC_VDEV_DETACH";
		case 0x810: return "ZFS_IOC_VDEV_SETPATH";
		case 0x811: return "ZFS_IOC_VDEV_SETFRU";
		case 0x812: return "ZFS_IOC_OBJSET_STATS";
		case 0x813: return "ZFS_IOC_OBJSET_ZPLPROPS";
		case 0x814: return "ZFS_IOC_DATASET_LIST_NEXT";
		case 0x815: return "ZFS_IOC_SNAPSHOT_LIST_NEXT";
		case 0x816: return "ZFS_IOC_SET_PROP";
		case 0x817: return "ZFS_IOC_CREATE";
		case 0x818: return "ZFS_IOC_DESTROY";
		case 0x819: return "ZFS_IOC_ROLLBACK";
		case 0x81a: return "ZFS_IOC_RENAME";
		case 0x81b: return "ZFS_IOC_RECV";
		case 0x81c: return "ZFS_IOC_SEND";
		case 0x81d: return "ZFS_IOC_INJECT_FAULT";
		case 0x81e: return "ZFS_IOC_CLEAR_FAULT";
		case 0x81f: return "ZFS_IOC_INJECT_LIST_NEXT";
		case 0x820: return "ZFS_IOC_ERROR_LOG";
		case 0x821: return "ZFS_IOC_CLEAR";
		case 0x822: return "ZFS_IOC_PROMOTE";
		case 0x823: return "ZFS_IOC_SNAPSHOT";
		case 0x824: return "ZFS_IOC_DSOBJ_TO_DSNAME";
		case 0x825: return "ZFS_IOC_OBJ_TO_PATH";
		case 0x826: return "ZFS_IOC_POOL_SET_PROPS";
		case 0x827: return "ZFS_IOC_POOL_GET_PROPS";
		case 0x828: return "ZFS_IOC_SET_FSACL";
		case 0x829: return "ZFS_IOC_GET_FSACL";
		case 0x82a: return "ZFS_IOC_SHARE";
		case 0x82b: return "ZFS_IOC_INHERIT_PROP";
		case 0x82c: return "ZFS_IOC_SMB_ACL";
		case 0x82d: return "ZFS_IOC_USERSPACE_ONE";
		case 0x82e: return "ZFS_IOC_USERSPACE_MANY";
		case 0x82f: return "ZFS_IOC_USERSPACE_UPGRADE";
		case 0x830: return "ZFS_IOC_HOLD";
		case 0x831: return "ZFS_IOC_RELEASE";
		case 0x832: return "ZFS_IOC_GET_HOLDS";
		case 0x833: return "ZFS_IOC_OBJSET_RECVD_PROPS";
		case 0x834: return "ZFS_IOC_VDEV_SPLIT";
		case 0x835: return "ZFS_IOC_NEXT_OBJ";
		case 0x836: return "ZFS_IOC_DIFF";
		case 0x837: return "ZFS_IOC_TMP_SNAPSHOT";
		case 0x838: return "ZFS_IOC_OBJ_TO_STATS";
		case 0x839: return "ZFS_IOC_SPACE_WRITTEN";
		case 0x83a: return "ZFS_IOC_SPACE_SNAPS";
		case 0x83b: return "ZFS_IOC_DESTROY_SNAPS";
		case 0x83c: return "ZFS_IOC_POOL_REGUID";
		case 0x83d: return "ZFS_IOC_POOL_REOPEN";
		case 0x83e: return "ZFS_IOC_SEND_PROGRESS";
		case 0x83f: return "ZFS_IOC_LOG_HISTORY";
		case 0x840: return "ZFS_IOC_SEND_NEW";
		case 0x841: return "ZFS_IOC_SEND_SPACE";
		case 0x842: return "ZFS_IOC_CLONE";
		case 0x843: return "ZFS_IOC_BOOKMARK";
		case 0x844: return "ZFS_IOC_GET_BOOKMARKS";
		case 0x845: return "ZFS_IOC_DESTROY_BOOKMARKS";
		case 0x846: return "ZFS_IOC_LOAD_KEY";
		case 0x847: return "ZFS_IOC_UNLOAD_KEY";
		case 0x848: return "ZFS_IOC_CHANGE_KEY";
		case 0x849: return "ZFS_IOC_REMAP";
		case 0x84a: return "ZFS_IOC_POOL_CHECKPOINT";
		case 0x84b: return "ZFS_IOC_POOL_DISCARD_CHECKPOINT";
		case 0x84c: return "ZFS_IOC_POOL_INITIALIZE";
		case 0x84d: return "ZFS_IOC_POOL_SYNC";
		case 0x84e: return "ZFS_IOC_CHANNEL_PROGRAM";
		case 0x84f: return "ZFS_IOC_TRIM";

		case 0x880: return "ZFS_IOC_EVENTS_NEXT";
		case 0x881: return "ZFS_IOC_EVENTS_CLEAR";
		case 0x882: return "ZFS_IOC_EVENTS_SEEK";

		case 0x8E0: return "ZFS_IOC_MOUNT";
		case 0x8E1: return "ZFS_IOC_UNMOUNT";
		case 0x8E2: return "ZFS_IOC_UNREGISTER_FS";

		case 0x8E3: return "ZFS_IOC_LAST";
		default: return "unkown";
	}
}


int vasprintf(char **strp, const char *fmt, va_list ap)
{
	int r = -1, size;

	size = _vscprintf(fmt, ap);

	if ((size >= 0) && (size < INT_MAX)) {
		*strp = (char *)malloc(size + 1);
		if (*strp) {
			r = vsnprintf(*strp, size + 1, fmt, ap);
			if ((r < 0) || (r > size)) {
				r = -1;
				free(*strp);
			}
		}
	} else {
		*strp = 0;
	}

	return(r);
}


int asprintf(char **strp, const char *fmt, ...)
{
	int r;
	va_list ap;
	va_start(ap, fmt);
	r = vasprintf(strp, fmt, ap);
	va_end(ap);
	return(r);
}


int gettimeofday(struct timeval *tp, struct timezone *tzp)
{
	// Note: some broken versions only have 8 trailing zero's, the correct epoch has 9 trailing zero's
	static const uint64_t EPOCH = ((uint64_t)116444736000000000ULL);

	SYSTEMTIME  system_time;
	FILETIME    file_time;
	uint64_t    time;

	GetSystemTime(&system_time);
	SystemTimeToFileTime(&system_time, &file_time);
	time = ((uint64_t)file_time.dwLowDateTime);
	time += ((uint64_t)file_time.dwHighDateTime) << 32;

	tp->tv_sec = (long)((time - EPOCH) / 10000000L);
	tp->tv_usec = (long)(system_time.wMilliseconds * 1000);
	return 0;
}


void flockfile(FILE *file)
{
}

void funlockfile(FILE *file)
{
}

unsigned long gethostid(void)
{
	LSTATUS Status;
	unsigned long hostid = 0UL;
	HKEY key;
	DWORD type;
	DWORD len;

	Status = RegOpenKeyEx(HKEY_LOCAL_MACHINE, "SYSTEM\\ControlSet001\\Services\\ZFSin",
		0, KEY_READ, &key);
	if (Status != ERROR_SUCCESS)
		return 0UL;

	len = sizeof(hostid);
	Status = RegQueryValueEx(key, "hostid", NULL, &type, (LPBYTE)&hostid, &len);
	if (Status != ERROR_SUCCESS)
		hostid = 0;

	assert(type == REG_DWORD);

	RegCloseKey(key);

	return (hostid & 0xffffffff);
}

uid_t geteuid(void)
{
	return 0; // woah, root?
}

struct passwd *getpwuid(uid_t uid)
{
	return NULL;
}

const char *win_ctime_r(char *buffer, size_t bufsize, time_t cur_time)
{
	errno_t e = ctime_s(buffer, bufsize, &cur_time);
	return buffer;
}

uint64_t GetFileDriveSize(HANDLE h)
{
	LARGE_INTEGER large;

	if (GetFileSizeEx(h, &large))
		return large.QuadPart;

	PARTITION_INFORMATION_EX partInfo;
	DWORD retcount = 0;

	if (DeviceIoControl(h,
		IOCTL_DISK_GET_PARTITION_INFO_EX,
		(LPVOID)NULL,
		(DWORD)0,
		(LPVOID)&partInfo,
		sizeof(partInfo),
		&retcount,
		(LPOVERLAPPED)NULL)) {
		return partInfo.PartitionLength.QuadPart;
	}


	DISK_GEOMETRY_EX geometry_ex;
	DWORD len;
	if (DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0,
		&geometry_ex, sizeof(geometry_ex), &len, NULL))
		return geometry_ex.DiskSize.QuadPart;

	return 0;
}


void
openlog(const char *ident, int logopt, int facility)
{

}

void
syslog(int priority, const char *message, ...)
{

}

void
closelog(void)
{

}

int
pipe(int fildes[2])
{
	return wosix_socketpair(AF_UNIX, SOCK_STREAM, 0, fildes);
}

struct group *
	getgrgid(gid_t gid)
{
	return NULL;
}

int
unmount(const char *dir, int flags)
{
	return -1;
}

extern size_t
strlcpy(register char* s, register const char* t, register size_t n)
{
	const char*     o = t;

	if (n)
		do
		{
			if (!--n)
			{
				*s = 0;
				break;
			}
		} while (*s++ = *t++);
		if (!n)
			while (*t++);
		return t - o - 1;
}

extern size_t
strlcat(register char* s, register const char* t, register size_t n)
{
	register size_t m;
	const char*     o = t;

	if (m = n)
	{
		while (n && *s)
		{
			n--;
			s++;
		}
		m -= n;
		if (n)
			do
			{
				if (!--n)
				{
					*s = 0;
					break;
				}
			} while (*s++ = *t++);
		else
			*s = 0;
	}
	if (!n)
		while (*t++);
	return (t - o) + m - 1;
}

char *strndup(char *src, size_t size)
{
	char *r = _strdup(src);
	if (r) {
		r[size] = 0;
	}
	return r;
}

int setrlimit(int resource, const struct rlimit *rlp)
{
	return 0;
}

int tcgetattr(int fildes, struct termios *termios_p)
{
	return 0;
}

int tcsetattr(int fildes, int optional_actions,
	const struct termios *termios_p)
{
	return 0;
}


void console_echo(boolean_t willecho)
{
	HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
	int constype = isatty(hStdin);
	switch (constype) {
	case 0:
	default:
		return;
	case 1: // dosbox
		if (willecho) {
			DWORD mode = 0;
			GetConsoleMode(hStdin, &mode);
			SetConsoleMode(hStdin, mode | (ENABLE_ECHO_INPUT));
		} else {
			DWORD mode = 0;
			GetConsoleMode(hStdin, &mode);
			SetConsoleMode(hStdin, mode & (~ENABLE_ECHO_INPUT));
		}
		return;
	case 2: // mingw/cygwin
		// Insert magic here
		return;
	}
}

// Not really getline, just used for password input in libzfs_crypto.c
#define MAX_GETLINE 128
ssize_t getline(char **linep, size_t* linecapp,
	FILE *stream)
{
	static char getpassbuf[MAX_GETLINE + 1];
	size_t i = 0;

	console_echo(FALSE);

	int c;
	for (;;)
	{
		c = getc(stream);
		if ((c == '\r') || (c == '\n'))
		{
			getpassbuf[i] = '\0';
			break;
		}
		else if (i < MAX_GETLINE)
		{
			getpassbuf[i++] = c;
		}
		if (i >= MAX_GETLINE)
		{
			getpassbuf[i] = '\0';
			break;
		}
	}

	if (linep) *linep = strdup(getpassbuf);
	if (linecapp) *linecapp = 1;

	console_echo(TRUE);

	return i;
}


/* Windows POSIX wrappers */


int wosix_fsync(int fd)
{
	if (!FlushFileBuffers(ITOH(fd)))
		return EIO;
	return 0;
}

int wosix_open(const char *path, int oflag, ...)
{
	HANDLE h;
	DWORD mode = GENERIC_READ; // RDONLY=0, WRONLY=1, RDWR=2;
	DWORD how = OPEN_EXISTING;
	DWORD share = FILE_SHARE_READ;
	// This is wrong, not all bitfields
	if (oflag&O_WRONLY) mode = GENERIC_WRITE;
	if (oflag&O_RDWR)   mode = GENERIC_READ | GENERIC_WRITE;

	switch (oflag&(O_CREAT | O_TRUNC | O_EXCL)) {
	case O_CREAT:
		how = OPEN_ALWAYS;
		break;
	case O_TRUNC:
		how = TRUNCATE_EXISTING;
		break;
	case (O_CREAT | O_EXCL):
	case (O_CREAT | O_EXCL | O_TRUNC): // Only creating new implies starting from 0
		how = CREATE_NEW;
		break;
	case (O_CREAT | O_TRUNC):
		how = CREATE_ALWAYS;
		break;
	default:
	case O_EXCL: // Invalid, ignore bit - treat as normal open
		how = OPEN_EXISTING;
		break;
	}
	if (oflag&O_APPEND) mode |= FILE_APPEND_DATA;

#ifdef O_EXLOCK
	if (!oflag&O_EXLOCK) share |= FILE_SHARE_WRITE;
#endif

	h = CreateFile(path, mode, share, NULL, how, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) {
		errno = EINVAL;
		switch (GetLastError()) {
		case ERROR_FILE_NOT_FOUND:
		case ERROR_PATH_NOT_FOUND:
			errno = ENOENT;
			break;
		case ERROR_ACCESS_DENIED:
			errno = EACCES;
			break;
		case ERROR_FILE_EXISTS:
			errno = EEXIST;
			break;
		}
		return -1;
	}
	return (HTOI(h));
}

// Figure out when to call WSAStartup();
static int posix_init_winsock = 0;

int wosix_close(int fd)
{
	HANDLE h = ITOH(fd);

	// Use CloseHandle() for everything except sockets.
	if ((GetFileType(h) == FILE_TYPE_REMOTE) &&
		!GetNamedPipeInfo(h, NULL, NULL, NULL, NULL)) {
		int err;
		err = closesocket((SOCKET)h);
		return err;
	}

	if (CloseHandle(h))
		return 0;
	return -1;
}

int wosix_ioctl(int fd, unsigned long request, zfs_cmd_t *zc)
{
	int error;
	ULONG bytesReturned;

	error = DeviceIoControl(ITOH(fd),
		(DWORD)request,
		zc,
		(DWORD)sizeof(zfs_cmd_t),
		zc,
		(DWORD)sizeof(zfs_cmd_t),
		&bytesReturned,
		NULL
	);

	if (error == 0)
		error = GetLastError();
	else
		error = 0;
	
#ifdef DEBUG
	fprintf(stderr, "    (ioctl 0x%x (%s) status %d bytes %ld)\n", (request & 0x2ffc) >> 2, getIoctlAsString((request & 0x2ffc) >> 2), error, bytesReturned); fflush(stderr);
#endif
#if 0
	for (int x = 0; x < 16; x++)
		fprintf(stderr, "%02x ", ((unsigned char *)zc)[x]);
	fprintf(stderr, "\n");
	fflush(stderr);
	fprintf(stderr, "returned ioctl on 0x%x (raw 0x%x) struct size %d in %p:%d out %p:%d\n",
		(request & 0x2ffc) >> 2, request,
		sizeof(zfs_cmd_t),
		zc->zc_nvlist_src, zc->zc_nvlist_src_size,
		zc->zc_nvlist_dst, zc->zc_nvlist_dst_size
	); fflush(stderr);
#endif
	errno = error;
	return error;
}

uint64_t wosix_lseek(int fd, uint64_t offset, int seek)
{
	LARGE_INTEGER LOFF, LNEW;
	int type = FILE_BEGIN;

	LOFF.QuadPart = offset;
	switch (seek) {
	case SEEK_SET:
		type = FILE_BEGIN;
		break;
	case SEEK_CUR:
		type = FILE_CURRENT;
		break;
	case SEEK_END:
		type = FILE_END;
		break;
	}
	if (!SetFilePointerEx(ITOH(fd), LOFF, &LNEW, type))
		return -1;
	return LNEW.QuadPart;
}

int wosix_read(int fd, void *data, uint32_t len)
{
	DWORD red;
	OVERLAPPED ow = {0};

	if (GetFileType(ITOH(fd)) == FILE_TYPE_PIPE) {
		if (!ReadFile(ITOH(fd), data, len, &red, &ow))
			return -1;
	} else {
		if (!ReadFile(ITOH(fd), data, len, &red, NULL))
			return -1;
	}

	return red;
}

int wosix_write(int fd, const void *data, uint32_t len)
{
	DWORD wrote;
	OVERLAPPED ow = { 0 };

	if (GetFileType(ITOH(fd)) == FILE_TYPE_PIPE) {
		if (!WriteFile(ITOH(fd), data, len, &wrote, &ow))
			return -1;
	} else {
		if (!WriteFile(ITOH(fd), data, len, &wrote, NULL))
			return -1;
	}
	return wrote;
}

#define is_wprefix(s, prefix) \
	(wcsncmp((s), (prefix), sizeof(prefix) / sizeof(WCHAR) - 1) == 0)

// Parts by:
// * Copyright(c) 2015 - 2017 K.Takata
// * You can redistribute it and /or modify it under the terms of either
// * the MIT license(as described below) or the Vim license.
//
// Extend isatty() slightly to return 1 for DOS Console, or
// 2 for cygwin/mingw - as we will have to do different things
// for NOECHO etc.
int wosix_isatty(int fd)
{
	DWORD mode;
	HANDLE h = ITOH(fd);
	int ret;

	// First, check if we are in a regular dos box, if yes, return.
	// If not, check for cygwin ...
	// check for mingw ...
	// check for powershell ...
	if (GetConsoleMode(h, &mode)) return 1;

	// Not CMDbox, check mingw
	if (GetFileType(h) == FILE_TYPE_PIPE) {

		int size = sizeof(FILE_NAME_INFO) + sizeof(WCHAR) * (MAX_PATH - 1);
		FILE_NAME_INFO* nameinfo;
		WCHAR* p = NULL;

		nameinfo = malloc(size + sizeof(WCHAR));
		if (nameinfo != NULL) {
			if (GetFileInformationByHandleEx(h, FileNameInfo, nameinfo, size)) {
				nameinfo->FileName[nameinfo->FileNameLength / sizeof(WCHAR)] = L'\0';
				p = nameinfo->FileName;
				if (is_wprefix(p, L"\\cygwin-")) {      /* Cygwin */
					p += 8;
				} else if (is_wprefix(p, L"\\msys-")) { /* MSYS and MSYS2 */
					p += 6;
				} else {
					p = NULL;
				}
				if (p != NULL) {
					while (*p && isxdigit(*p))  /* Skip 16-digit hexadecimal. */
						++p;
					if (is_wprefix(p, L"-pty")) {
						p += 4;
					} else {
						p = NULL;
					}
				}
				if (p != NULL) {
					while (*p && isdigit(*p))   /* Skip pty number. */
						++p;
					if (is_wprefix(p, L"-from-master")) {
						//p += 12;
					} else if (is_wprefix(p, L"-to-master")) {
						//p += 10;
					} else {
						p = NULL;
					}
				}
			}
			free(nameinfo);
			if (p != NULL)
				return 2;
		}
	}

	// Give up, it's not a TTY
	return 0;
}

// A bit different, just to wrap away the second argument
// Presumably _mkdir() sets errno, as EEXIST is tested.
int wosix_mkdir(const char *path, mode_t mode)
{
	return _mkdir(path);
}

int wosix_stat(char *path, struct _stat64 *st)
{
	int fd;
	int ret;
	fd = open(path, O_RDONLY);
	if (fd == -1)
		return -1;
	ret = wosix_fstat(fd, st);
        close(fd);
        return (ret);
}

int wosix_lstat(char *path, struct _stat64 *st)
{
	int fd;
	int ret;

	fd = open(path, O_RDONLY);
	if (fd == -1)
		return -1;
	ret = wosix_fstat(fd, st); // Fix me? Symlinks
	close(fd);
	return (ret);
}

// Only fill in what we actually use in ZFS
// Mostly used to test for existance, st_mode, st_size
// also FIFO and BLK (fixme)
int wosix_fstat(int fd, struct _stat64 *st)
{
	HANDLE h = ITOH(fd);
	BY_HANDLE_FILE_INFORMATION info;

	if (!GetFileInformationByHandle(h, &info))
		return wosix_fstat_blk(fd, st); 

	st->st_dev = 0;
	st->st_ino = 0;
	st->st_mode = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ?
		_S_IFDIR : _S_IFREG;
	st->st_nlink = (info.nNumberOfLinks > SHRT_MAX ? SHRT_MAX : info.nNumberOfLinks);
	st->st_uid = 0;
	st->st_gid = 0;
	st->st_rdev = 0;
	st->st_size = ((long long)info.nFileSizeHigh << 32ULL) | (long long)info.nFileSizeLow;
	st->st_atime = 0;
	st->st_mtime = 0;
	st->st_ctime = 0;

	return 0;
}

int wosix_fstat_blk(int fd, struct _stat64 *st)
{
	DISK_GEOMETRY_EX geometry_ex;
	HANDLE handle = ITOH(fd);
	DWORD len;

	if (!DeviceIoControl(handle, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0,
		&geometry_ex, sizeof(geometry_ex), &len, NULL))
		return -1; // errno?

	st->st_size = (diskaddr_t)geometry_ex.DiskSize.QuadPart;
#ifndef _S_IFBLK
#define	_S_IFBLK	0x3000
#endif
	st->st_mode = _S_IFBLK;

	return (0);
}

// os specific files can call this directly.
int pread_win(HANDLE h, void *buf, size_t nbyte, off_t offset)
{
	uint64_t off;
	DWORD red;
	LARGE_INTEGER large;
	LARGE_INTEGER lnew;
	// This code does all seeks based on "current" so we can pre-seek to offset start

	// Find current position
	large.QuadPart = 0;
	SetFilePointerEx(h, large, &lnew, FILE_CURRENT);

	// Seek to place to read
	large.QuadPart = offset;
	SetFilePointerEx(h, large, NULL, FILE_BEGIN);

	boolean_t ok;

	ok = ReadFile(h, buf, nbyte, &red, NULL);

	if (!ok) {
		red = GetLastError();
		red = -red;
	}

	// Restore position
	SetFilePointerEx(h, lnew, NULL, FILE_BEGIN);

	return (red);
}

int wosix_pread(int fd, void *buf, size_t nbyte, off_t offset)
{
	return pread_win(ITOH(fd), buf, nbyte, offset);
}

int wosix_pwrite(int fd, const void *buf, size_t nbyte, off_t offset)
{
	HANDLE h = ITOH(fd);
	uint64_t off;
	DWORD wrote;
	LARGE_INTEGER large;
	LARGE_INTEGER lnew;

	// This code does all seeks based on "current" so we can pre-seek to offset start

	// Find current position
	large.QuadPart = 0;
	SetFilePointerEx(h, large, &lnew, FILE_CURRENT);

	// Seek to place to read
	large.QuadPart = offset;
	SetFilePointerEx(h, large, NULL, FILE_BEGIN);

	// Write
	if (!WriteFile(h, buf, nbyte, &wrote, NULL))
		wrote = -GetLastError();

	// Restore position
	SetFilePointerEx(h, lnew, NULL, FILE_BEGIN);

	return wrote;
}

int wosix_fdatasync(int fd)
{
	//if (fcntl(fd, F_FULLFSYNC) == -1)
	//	return -1;
	return 0;
}

int wosix_ftruncate(int fd, off_t length)
{
	HANDLE h = ITOH(fd);
	LARGE_INTEGER lnew;

	lnew.QuadPart = length;
	if (SetFilePointerEx(h, lnew, NULL, FILE_BEGIN) &&
		SetEndOfFile(h))
		return 0; // Success
	// errno?
	return -1;
}

FILE *wosix_fdopen(int fd, const char *mode)
{
	// Convert HANDLE to int
	int temp = _open_osfhandle((intptr_t)ITOH(fd), _O_APPEND | _O_RDONLY);

	if (temp == -1) {
		return NULL;
	}

	// Convert int to FILE*
	FILE *f = _fdopen(temp, mode);

	if (f == NULL) {
		_close(temp);
		return NULL;
	}

	// Why is this print required?
	fprintf(stderr, "\r\n");

	// fclose(f) will also call _close() on temp.
	return (f);
}

int wosix_socketpair(int domain, int type, int protocol, int sv[2])
{
	int temp, s1, s2, result;
	struct sockaddr_in saddr;
	int nameLen;
	unsigned long option_arg = 1;
	int err = 0;
	WSADATA wsaData;

	// Do we need to init winsock? Is this the right way, should we
	// add _init/_exit calls? If socketpair is the only winsock call
	// we have, this might be ok.
	if (posix_init_winsock == 0) {
		posix_init_winsock = 1;
		err = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (err != 0) {
			errno = err;
			return -1;
		}
	}

	nameLen = sizeof(saddr);

	/* ignore address family for now; just stay with AF_INET */
	temp = socket(AF_INET, SOCK_STREAM, 0);
	if (temp == INVALID_SOCKET) {
		int err = WSAGetLastError();
		errno = err;
		return -1;
	}

	setsockopt(temp, SOL_SOCKET, SO_REUSEADDR, (void *)&option_arg,
		sizeof(option_arg));

	/* We *SHOULD* choose the correct sockaddr structure based
	on the address family requested... */
	memset(&saddr, 0, sizeof(saddr));

	saddr.sin_family = AF_INET;
	saddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	saddr.sin_port = 0; // give me a port

	result = bind(temp, (struct sockaddr *)&saddr, nameLen);
	if (result == SOCKET_ERROR) {
		errno = WSAGetLastError();
		closesocket(temp);
		return -2;
	}

	// Don't care about error here, the connect will fail instead
	listen(temp, 1);

	// Fetch out the port that was given to us.
	nameLen = sizeof(struct sockaddr_in);

	result = getsockname(temp, (struct sockaddr *)&saddr, &nameLen);

	if (result == INVALID_SOCKET) {
		closesocket(temp);
		return -4; /* error case */
	}

	s1 = socket(AF_INET, SOCK_STREAM, 0);
	if (s1 == INVALID_SOCKET) {
		closesocket(temp);
		return -5;
	}

	nameLen = sizeof(struct sockaddr_in);

	result = connect(s1, (struct sockaddr *)&saddr, nameLen);

	if (result == INVALID_SOCKET) {
		closesocket(temp);
		closesocket(s1);
		return -6; /* error case */
	}

	s2 = accept(temp, NULL, NULL);

	closesocket(temp);

	if (s2 == INVALID_SOCKET) {
		closesocket(s1);
		return -7;
	}

	sv[0] = s1; sv[1] = s2;

	if ((sv[0] < 0) || (sv[1] < 0)) return -8;

	return 0;  /* normal case */
}

int wosix_dup2(int fildes, int fildes2)
{
	return -1;
}

static long GetLogicalProcessors(void);

long
sysconf(int name)
{
	SYSTEM_INFO info;
	MEMORYSTATUSEX status;
 
	switch (name) {

	case _SC_NPROCESSORS_ONLN:
		return GetLogicalProcessors();
	case _SC_PHYS_PAGES:
	case _SC_PAGE_SIZE:
		GetSystemInfo(&info);
		if (name == _SC_PAGE_SIZE)
			return info.dwPageSize;
		status.dwLength = sizeof(status);
		GlobalMemoryStatusEx( &status );
		return (long)(status.ullTotalPhys / info.dwPageSize);
	default:
		return (-1);
	}
}


typedef BOOL (WINAPI *LPFN_GLPI)(
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION, 
    PDWORD);

// Helper function to count set bits in the processor mask.
static DWORD CountSetBits(ULONG_PTR bitMask)
{
    DWORD LSHIFT = sizeof(ULONG_PTR)*8 - 1;
    DWORD bitSetCount = 0;
    ULONG_PTR bitTest = (ULONG_PTR)1 << LSHIFT;    
    DWORD i;
    
    for (i = 0; i <= LSHIFT; ++i)
    {
        bitSetCount += ((bitMask & bitTest)?1:0);
        bitTest/=2;
    }

    return bitSetCount;
}

static long
GetLogicalProcessors(void)
{
	LPFN_GLPI glpi;
	BOOL done = FALSE;
	PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer = NULL;
	PSYSTEM_LOGICAL_PROCESSOR_INFORMATION ptr = NULL;
	DWORD returnLength = 0;
	DWORD logicalProcessorCount = 0;
	DWORD numaNodeCount = 0;
	DWORD processorCoreCount = 0;
	DWORD processorL1CacheCount = 0;
	DWORD processorL2CacheCount = 0;
	DWORD processorL3CacheCount = 0;
	DWORD processorPackageCount = 0;
	DWORD byteOffset = 0;
	PCACHE_DESCRIPTOR Cache;

	glpi = (LPFN_GLPI) GetProcAddress(
		GetModuleHandle(TEXT("kernel32")),
		"GetLogicalProcessorInformation");
	if (NULL == glpi) 
		return (-1);

	while (!done) {
		DWORD rc = glpi(buffer, &returnLength);

		if (FALSE == rc) {
			if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
				if (buffer) 
					free(buffer);

				buffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)malloc(
				    returnLength);

				if (NULL == buffer) 
					return (-1);
			} else {
				return (-1);
			}
		} else {
			done = TRUE;
		}
	}

	ptr = buffer;

	while (byteOffset + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) <= returnLength) {
		switch (ptr->Relationship) {
			case RelationNumaNode:
			// Non-NUMA systems report a single record of this type.
			numaNodeCount++;
			break;

		case RelationProcessorCore:
			processorCoreCount++;

			// A hyperthreaded core supplies more than one logical processor.
			logicalProcessorCount += CountSetBits(ptr->ProcessorMask);
			break;

		case RelationCache:
			// Cache data is in ptr->Cache, one CACHE_DESCRIPTOR structure for each cache. 
			Cache = &ptr->Cache;
			if (Cache->Level == 1)
				processorL1CacheCount++;
			else if (Cache->Level == 2)
				processorL2CacheCount++;
			else if (Cache->Level == 3)
				processorL3CacheCount++;
			break;

		case RelationProcessorPackage:
			// Logical processors share a physical package.
			processorPackageCount++;
			break;

		default:
			break;
		}
	byteOffset += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
	ptr++;
	}

	free(buffer);
	return (logicalProcessorCount);
}

int mprotect(void *addr, size_t len, int prot)
{
	// We can probably implement something using VirtualProtect() here.
	return (0);
}

int getuid (void)
{
	return 1;
}


int
fcntl(int fildes, int cmd, /* arg */ ...)
{
    return (0);
}

int
sched_yield(void)
{
	Sleep(0);
	return (0);
}

int
uname(struct utsname *buf)
{
	OSVERSIONINFOEX versionex;
	SYSTEM_INFO info;
	/* Fill in nodename.  */
	if (gethostname(buf->nodename, sizeof (buf->nodename)) < 0)
		strcpy(buf->nodename, "localhost");

	versionex.dwOSVersionInfoSize = sizeof (OSVERSIONINFOEX);
	GetVersionEx(&versionex);
	snprintf(buf->sysname, sizeof (buf->sysname), "Windows_NT-%u.%u",
	    (unsigned int) versionex.dwMajorVersion,
	    (unsigned int) versionex.dwMinorVersion);


	GetSystemInfo(&info);

	switch(info.wProcessorArchitecture) {
	case PROCESSOR_ARCHITECTURE_AMD64:
		strcpy(buf->machine, "x86_64");
		break;
	case PROCESSOR_ARCHITECTURE_IA64:
		strcpy(buf->machine, "ia64");
		break;
	case PROCESSOR_ARCHITECTURE_INTEL:
		strcpy(buf->machine, "i386");
		break;
	default:
		strcpy(buf->machine, "unknown");
		break;
	}

	return (0);
}

char *
nl_langinfo(nl_item item)
{
	switch (item)
	{
	/* nl_langinfo items of the LC_CTYPE category */
	case _DATE_FMT:
		 "%y/%m/%d";
	}
	return "";
}

/*
 * The port of openat() is quite half-hearted. But it is currently
 * only used with opendir(), and not used to create "..." nor with "fd".
 */ 
int
wosix_openat(int fd, const char* path, int oflag, ...)
{
    if (fd == AT_FDCWD)
	return wosix_open(path, oflag);
    ASSERT("openat() implementation lacking support");
    return (-1);
}
