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
 * Copyright (c) 2017 Jorgen Lundman <lundman@lundman.net.
 */
#define	_LARGEFILE64_SOURCE
#define	_FILE_OFFSET_BITS 64
#include <WinSock2.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/types32.h>
#include <time.h>
#include <io.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/mntent.h>
#include <sys/mount.h>
#include <fcntl.h>
// #include <sys/zfs_ioctl.h>
#include <pthread.h>
#include <Windows.h>
#include <langinfo.h>
// #include <os/windows/zfs/sys/zfs_ioctl_compat.h>
#include <sys/mman.h>
#include <sys/mnttab.h>
#include <sys/utsname.h>
#include <sys/uio.h>
#include <termios.h>
#include <wfunopen.h>
#include <pwd.h>
#include <grp.h>
#include <search.h>

/* Magic instruction to compiler to add library */
#pragma comment(lib, "ws2_32.lib")

/*
 * Windows needs the winsock2 code to be initialised before
 * use, and we don't really know who will be called first.
 */
static __attribute__((constructor)) void
posix_init_winsock(void)
{
	WSADATA wsaData;
	int ret;
	ret = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (ret != 0)
		fprintf(stderr, "Initialising winsock2 failed: %d\r\n", ret);
}

void
clock_gettime(clock_type_t t, struct timespec *ts)
{
	LARGE_INTEGER time;
	LARGE_INTEGER frequency;
	FILETIME ft;

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

int
posix_memalign(void **memptr, size_t alignment, size_t size)
{
	void *ptr;
	ptr = _aligned_malloc(size, alignment);
	if (ptr == NULL)
		return (ENOMEM);
	*memptr = ptr;
	return (0);
}

const char *
getexecname(void)
{
	__declspec(thread) static char execname[32767 + 1];
	GetModuleFileNameA(NULL, execname, sizeof (execname));
	return (execname);
}

/*
 * Map a Windows SID to a POSIX uid using the same scheme as the kernel's
 * spl_sid_to_uid().  Uses Win32 SID accessor macros (no direct struct access).
 */
static uid_t
win_sid_to_uid(SID *sid)
{
	SID_IDENTIFIER_AUTHORITY *auth = GetSidIdentifierAuthority(sid);
	UCHAR nsub = *GetSidSubAuthorityCount(sid);

	/* S-1-5-18 (SYSTEM) -> root */
	if (auth->Value[5] == 5 && nsub == 1 &&
	    *GetSidSubAuthority(sid, 0) == 18)
		return (0);

	/* S-1-22-1-X (Samba unix-user) */
	if (auth->Value[5] == 22 && nsub == 2 &&
	    *GetSidSubAuthority(sid, 0) == 1)
		return ((uid_t)*GetSidSubAuthority(sid, 1));

	/* S-1-5-21-*-*-*-RID (domain / local account) */
	if (auth->Value[5] == 5 && nsub >= 5 &&
	    *GetSidSubAuthority(sid, 0) == 21)
		return ((uid_t)*GetSidSubAuthority(sid, nsub - 1));

	/* S-1-5-22-*-*-*-RID */
	if (auth->Value[5] == 5 && nsub >= 5 &&
	    *GetSidSubAuthority(sid, 0) == 22)
		return ((uid_t)*GetSidSubAuthority(sid, nsub - 1));

	return (65534); /* nobody */
}

/*
 * Map a Windows SID to a POSIX gid using the same scheme as the kernel's
 * spl_sid_to_gid().
 */
static gid_t
win_sid_to_gid(SID *sid)
{
	SID_IDENTIFIER_AUTHORITY *auth = GetSidIdentifierAuthority(sid);
	UCHAR nsub = *GetSidSubAuthorityCount(sid);

	/* S-1-22-2-0 / S-1-22-2-X (Samba unix-group) */
	if (auth->Value[5] == 22 && nsub >= 1 &&
	    *GetSidSubAuthority(sid, 0) == 2) {
		if (nsub == 1)
			return (0);
		return ((gid_t)*GetSidSubAuthority(sid, 1));
	}

	/* S-1-5-21-*-*-*-RID (domain / local group) */
	if (auth->Value[5] == 5 && nsub >= 5 &&
	    *GetSidSubAuthority(sid, 0) == 21)
		return ((gid_t)*GetSidSubAuthority(sid, nsub - 1));

	return (65534); /* nobody */
}

struct passwd *
getpwnam(const char *login)
{
	static __declspec(thread) struct passwd pw;
	static __declspec(thread) char name_buf[256];
	BYTE sid_buf[SECURITY_MAX_SID_SIZE];
	DWORD sid_size = sizeof (sid_buf);
	char domain[256];
	DWORD domain_size = sizeof (domain);
	SID_NAME_USE sid_use;

	if (!LookupAccountNameA(NULL, login, sid_buf, &sid_size,
	    domain, &domain_size, &sid_use))
		return (NULL);

	if (sid_use != SidTypeUser)
		return (NULL);

	strlcpy(name_buf, login, sizeof (name_buf));
	pw.pw_name = name_buf;
	pw.pw_passwd = "";
	pw.pw_uid = win_sid_to_uid((SID *)sid_buf);
	pw.pw_gid = pw.pw_uid;
	pw.pw_gecos = name_buf;
	pw.pw_dir = "";
	pw.pw_shell = "";
	return (&pw);
}

struct group *
getgrnam(const char *group)
{
	static __declspec(thread) struct group grp;
	static __declspec(thread) char name_buf[256];
	BYTE sid_buf[SECURITY_MAX_SID_SIZE];
	DWORD sid_size = sizeof (sid_buf);
	char domain[256];
	DWORD domain_size = sizeof (domain);
	SID_NAME_USE sid_use;

	if (!LookupAccountNameA(NULL, group, sid_buf, &sid_size,
	    domain, &domain_size, &sid_use))
		return (NULL);

	if (sid_use != SidTypeGroup && sid_use != SidTypeAlias &&
	    sid_use != SidTypeWellKnownGroup)
		return (NULL);

	strlcpy(name_buf, group, sizeof (name_buf));
	grp.gr_name = name_buf;
	grp.gr_passwd = "";
	grp.gr_gid = win_sid_to_gid((SID *)sid_buf);
	grp.gr_mem = NULL;
	return (&grp);
}

struct tm *
localtime_r(const time_t *clock, struct tm *result)
{
	if (localtime_s(result, clock) == 0)
		return (result);
	// To avoid the ASSERT and abort(), make tm be something valid
	memset(result, 0, sizeof (*result));
	result->tm_mday = 1;
	return (NULL);
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
	for (tok = s; /* empty */; ) {
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

char *
realpath(const char *file_name, char *resolved_name)
{
	wchar_t wfile[PATH_MAX], wresolved[PATH_MAX];

	if (resolved_name == NULL)
		resolved_name = malloc(PATH_MAX);
	if (resolved_name == NULL)
		return (NULL);

	if (MultiByteToWideChar(CP_UTF8, 0, file_name, -1,
	    wfile, PATH_MAX) == 0)
		return (NULL);
	if (GetFullPathNameW(wfile, PATH_MAX, wresolved, NULL) == 0)
		return (NULL);
	if (WideCharToMultiByte(CP_UTF8, 0, wresolved, -1,
	    resolved_name, PATH_MAX, NULL, NULL) == 0)
		return (NULL);

	/*
	 * GetFullPathNameW returns a trailing backslash for UNC share roots
	 * (e.g. \\server\share\).  Strip it so callers that concatenate a
	 * separator don't produce a double backslash.  Preserve drive roots
	 * (C:\, length == 3).
	 */
	size_t rlen = strlen(resolved_name);
	if (rlen > 3 && resolved_name[rlen - 1] == '\\')
		resolved_name[rlen - 1] = '\0';

	return (resolved_name);
}

int
statfs(const char *path, struct statfs *buf)
{
	ULARGE_INTEGER lpFreeBytesAvailable;
	ULARGE_INTEGER lpTotalNumberOfBytes;
	ULARGE_INTEGER lpTotalNumberOfFreeBytes;
	uint64_t lbsize;

#if 1
	if (GetDiskFreeSpaceEx(path,
	    &lpFreeBytesAvailable,
	    &lpTotalNumberOfBytes,
	    &lpTotalNumberOfFreeBytes)) {
		return (-1);
	}
#endif

	DISK_GEOMETRY_EX geometry_ex;
	HANDLE handle;
	DWORD len;

	handle = wosix_open(path, O_RDONLY | O_BINARY);

	if (!DeviceIoControl(handle, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0,
	    &geometry_ex, sizeof (geometry_ex), &len, NULL))
		return (-1);
	wosix_close(handle);
	lbsize = (uint_t)geometry_ex.Geometry.BytesPerSector;

	buf->f_bsize = lbsize;
	buf->f_blocks = lpTotalNumberOfBytes.QuadPart / lbsize;
	buf->f_bfree = lpTotalNumberOfFreeBytes.QuadPart / lbsize;
	buf->f_bavail = lpTotalNumberOfFreeBytes.QuadPart / lbsize;
	buf->f_type = 0;
	strcpy(buf->f_fstypename, "fixme");
	strcpy(buf->f_mntonname, "fixme_to");
	strcpy(buf->f_mntfromname, "fixme_from");

	return (0);
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

#define	ATTEMPTS_MIN (62 * 62 * 62)

#if ATTEMPTS_MIN < TMP_MAX
	unsigned int attempts = TMP_MAX;
#else
	unsigned int attempts = ATTEMPTS_MIN;
#endif

	len = strlen(tmpl);
	if (len < 6 || strcmp(&tmpl[len - 6], "XXXXXX")) {
		errno = EINVAL;
		return (-1);
	}

	XXXXXX = &tmpl[len - 6];

	{
		SYSTEMTIME stNow;
		FILETIME ftNow;

		// get system time
		GetSystemTime(&stNow);
		stNow.wMilliseconds = 500;
		if (!SystemTimeToFileTime(&stNow, &ftNow)) {
			errno = -1;
			return (-1);
		}

		random_time_bits =
		    (((unsigned long long)ftNow.dwHighDateTime << 32) |
		    (unsigned long long)ftNow.dwLowDateTime);
	}
	value += random_time_bits ^ (unsigned long long)GetCurrentThreadId();

	for (count = 0; count < attempts; value += 7777, ++count) {
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

		fd = open(tmpl,
		    O_RDWR | O_CREAT | O_EXCL, _S_IREAD | _S_IWRITE);
		if (fd >= 0) {
			errno = save_errno;
			return (fd);
		} else if (errno != EEXIST)
			return (-1);
	}

	/* We got out of the loop because we ran out of combinations to try.  */
	errno = EEXIST;
	return (-1);
}

int
mkostemps(char *template, int suffixlen, DWORD flags)
{
	// Generate a temporary file name
	char tempPath[MAX_PATH];
	GetTempPathA(MAX_PATH, tempPath);

	char tempFileName[MAX_PATH];
	if (GetTempFileNameA(tempPath, "temp", 0, tempFileName) == 0)
		return (-1);

	strcpy(template, tempFileName);
	// strncpy(template + strlen(template) - suffixlen, SUFFIX, suffixlen);

	// Open the file with desired flags
	HANDLE hFile = CreateFileA(template, GENERIC_READ | GENERIC_WRITE, 0,
	    NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY |
	    FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		return (-1);
	}
	return (HTOI(hFile));
}

int
readlink(const char *path, char *buf, size_t bufsize)
{
	return (-1);
}

int
usleep(__int64 usec)
{
	HANDLE timer;
	LARGE_INTEGER ft;

	ft.QuadPart = -(10 * usec);

	timer = CreateWaitableTimer(NULL, TRUE, NULL);
	SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
	WaitForSingleObject(timer, INFINITE);
	CloseHandle(timer);
	return (0);
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
		return (FALSE);

	/* Set timer properties */
	if (!SetWaitableTimer(timer, &li, 0, NULL, NULL, FALSE)) {
		CloseHandle(timer);
		return (FALSE);
	}
	/* Start & wait for timer */
	WaitForSingleObject(timer, INFINITE);
	/* Clean resources */
	CloseHandle(timer);
	/* Slept without problems */
	return (0);
}

int
strncasecmp(const char *s1, const char *s2, size_t n)
{
	if (n == 0)
		return (0);

	while (n-- != 0 && tolower(*s1) == tolower(*s2)) {
		if (n == 0 || *s1 == '\0' || *s2 == '\0')
			break;
		s1++;
		s2++;
	}

	return (tolower(*(unsigned char *)s1) - tolower(*(unsigned char *)s2));
}

char *
strchrnul(const char *p, int ch)
{
	for (; *p != 0 && *p != ch; p++)
		;
	return (__DECONST(char *, p));
}


/*
 * Find the first occurrence of find in s, where the search is limited to the
 * first slen characters of s.
 */
char *
strnstr(const char *s, const char *find, size_t slen)
{
	char c, sc;
	size_t len;

	if ((c = *find++) != '\0') {
		len = strlen(find);
		do {
			do {
				if (slen-- < 1 || (sc = *s++) == '\0')
					return (NULL);
			} while (sc != c);
			if (len > slen)
				return (NULL);
		} while (strncmp(s, find, len) != 0);
		s--;
	}
	return (__DECONST(char *, s));
}

#define	DIRNAME		0
#define	BASENAME	1

#define	M_FSDELIM(c)	((c) == '/' || (c) == '\\')
#define	M_DRDELIM(c)	(0)

static char curdir[] = ".";
static char *
basedir(char *arg, int type)
{
	register char *cp, *path;

	if (arg == (char *)0 || *arg == '\0' ||
	    (*arg == '.' && (arg[1] == '\0' ||
	    (type == DIRNAME && arg[1] == '.' && arg[2] == '\0'))))
		return (curdir);  /* arg NULL, empty, ".", or ".." in DIRNAME */

	if (M_DRDELIM(arg[1]))  /* drive-specified pathnames */
		path = arg + 2;
	else
		path = arg;

	if (path[1] == '\0'&&M_FSDELIM(*path))    /* "/", or drive analog */
		return (arg);

	cp = strchr(path, '\0');
	cp--;

	while (cp != path && M_FSDELIM(*cp))
		*(cp--) = '\0';

	for (; cp > path && !M_FSDELIM(*cp); cp--)
		;

	if (!M_FSDELIM(*cp))
		if (type == DIRNAME && path != arg) {
			*path = '\0';
			return (arg); /* curdir on the specified drive */
		} else
			return ((type == DIRNAME) ? curdir : path);
	else if (cp == path && type == DIRNAME) {
		cp[1] = '\0';
		return (arg); /* root directory involved */
	} else if (cp == path && cp[1] == '\0')
		return (arg);
	else if (type == BASENAME)
		return (++cp);
	*cp = '\0';
	return (arg);
}

char *
dirname(char *arg)
{
	return (basedir(arg, DIRNAME));
}

char *
basename(char *arg)
{
	return (basedir(arg, BASENAME));
}

char *
getIoctlAsString(int cmdNo)
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


int
vasprintf(char **strp, const char *fmt, va_list ap)
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

	return (r);
}


int
asprintf(char **strp, const char *fmt, ...)
{
	int r;
	va_list ap;
	va_start(ap, fmt);
	r = vasprintf(strp, fmt, ap);
	va_end(ap);
	return (r);
}


int
gettimeofday(struct timeval *tp, struct timezone *tzp)
{
	// Note: some broken versions only have 8 trailing zero's,
	// the correct epoch has 9 trailing zero's
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
	return (0);
}


void
flockfile(FILE *file)
{
}

void
funlockfile(FILE *file)
{
}

unsigned long
gethostid(void)
{
	LSTATUS Status;
	unsigned long hostid = 0UL;
	HKEY key;
	DWORD type;
	DWORD len;

	Status = RegOpenKeyEx(HKEY_LOCAL_MACHINE,
	    "SYSTEM\\ControlSet001\\Services\\OpenZFS",
	    0, KEY_READ, &key);
	if (Status != ERROR_SUCCESS)
		return (0UL);

	len = sizeof (hostid);
	Status = RegQueryValueEx(key, "spl_hostid", NULL, &type,
	    (LPBYTE)&hostid, &len);
	if (Status != ERROR_SUCCESS)
		hostid = 0;
	else
		assert(type == REG_DWORD);

	RegCloseKey(key);

	return (hostid & 0xffffffff);
}

uid_t
geteuid(void)
{
	BOOL elevated = FALSE;
	HANDLE token;
	if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
		TOKEN_ELEVATION elev;
		DWORD sz = sizeof (elev);
		if (GetTokenInformation(token, TokenElevation,
		    &elev, sz, &sz))
			elevated = elev.TokenIsElevated;
		CloseHandle(token);
	}
	return (elevated ? 0 : 1);
}

struct passwd *
getpwuid(uid_t uid)
{
	static __declspec(thread) struct passwd pw;
	static __declspec(thread) char name_buf[256];
	DWORD name_size = sizeof (name_buf);
	char domain[256];
	DWORD domain_size = sizeof (domain);
	SID_NAME_USE sid_use;

	/*
	 * Construct S-1-5-18 for uid 0 (SYSTEM / root).  Other uids would
	 * require knowing the machine/domain SID to reconstruct the full
	 * S-1-5-21-*-*-*-RID, so we leave them unresolved for now.
	 */
	if (uid != 0)
		return (NULL);

	SID_IDENTIFIER_AUTHORITY nt_auth = SECURITY_NT_AUTHORITY;
	BYTE sid_buf[SECURITY_MAX_SID_SIZE];
	PSID sid = (PSID)sid_buf;

	if (!AllocateAndInitializeSid(&nt_auth, 1,
	    SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0, &sid))
		return (NULL);

	BOOL ok = LookupAccountSidA(NULL, sid, name_buf, &name_size,
	    domain, &domain_size, &sid_use);
	FreeSid(sid);

	if (!ok)
		return (NULL);

	pw.pw_name = name_buf;
	pw.pw_passwd = "";
	pw.pw_uid = 0;
	pw.pw_gid = 0;
	pw.pw_gecos = name_buf;
	pw.pw_dir = "";
	pw.pw_shell = "";
	return (&pw);
}

const char *
win_ctime_r(char *buffer, size_t bufsize, time_t cur_time)
{
	ctime_s(buffer, bufsize, cur_time);
	return (buffer);
}

uint64_t
GetFileDriveSize(HANDLE h)
{
	LARGE_INTEGER large;

	if (GetFileSizeEx(h, &large))
		return (large.QuadPart);

	PARTITION_INFORMATION_EX partInfo;
	DWORD retcount = 0;

	if (DeviceIoControl(h,
	    IOCTL_DISK_GET_PARTITION_INFO_EX,
	    (LPVOID)NULL,
	    (DWORD)0,
	    (LPVOID)&partInfo,
	    sizeof (partInfo),
	    &retcount,
	    (LPOVERLAPPED)NULL)) {
		return (partInfo.PartitionLength.QuadPart);
	}


	DISK_GEOMETRY_EX geometry_ex;
	DWORD len;
	if (DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0,
	    &geometry_ex, sizeof (geometry_ex), &len, NULL))
		return (geometry_ex.DiskSize.QuadPart);

	return (0);
}


void
openlog(const char *ident, int logopt, int facility)
{

}

#define	ZEDLOG ZFSEXECDIR "/zed.txt"
void
syslog(int priority, const char *message, ...)
{
	FILE *fd;
	fd = fopen(ZEDLOG, "a");
	if (fd != NULL) {
		va_list args;
		va_start(args, message);
		vfprintf(fd, message, args);
		va_end(args);
		fclose(fd);
	} else {
		int ret = GetLastError();
		printf("%d\n", ret);
	}
}

void
closelog(void)
{

}

int
pipe(int fildes[2])
{
	return (wosix_socketpair(AF_UNIX, SOCK_STREAM, 0, fildes));
}

struct group *
getgrgid(gid_t gid)
{
	return (NULL);
}

int
unmount(const char *dir, int flags)
{
	return (-1);
}

extern size_t
strlcpy(register char *s, register const char *t, register size_t n)
{
	const char *o = t;

	if (n)
		do {
			if (!--n) {
				*s = 0;
				break;
			}
		} while ((*s++ = *t++));
	if (!n)
		while (*t++)
			;
	return (t - o - 1);
}

extern size_t
strlcat(register char *s, register const char *t, register size_t n)
{
	register size_t m;
	const char *o = t;

	if ((m = n)) {
		while (n && *s)	{
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
			} while ((*s++ = *t++));
		else
			*s = 0;
	}
	if (!n)
		while (*t++)
			;
	return ((t - o) + m - 1);
}

char *
strndup(const char *src, size_t size)
{
	char *r = _strdup(src);
	if (r) {
		r[size] = 0;
	}
	return (r);
}

int
setrlimit(int resource, const struct rlimit *rlp)
{
	return (0);
}

int
tcgetattr(int fildes, struct termios *termios_p)
{
	return (0);
}

int
tcsetattr(int fildes, int optional_actions,
    const struct termios *termios_p)
{
	return (0);
}


void
console_echo(boolean_t willecho)
{
	HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
	int constype = isatty(HTOI(hStdin));
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
#define	MAX_GETLINE 128
ssize_t
getline_impl(char **linep, size_t *linecapp,
    FILE *stream, boolean_t internal)
{
	static char getpassbuf[MAX_GETLINE + 1];
	size_t i = 0;
	fakeFILE *fFILE = (fakeFILE *)stream;

	console_echo(FALSE);

	int c;
	for (;;) {
		if (internal)
			fFILE->readfn(fFILE->cookie, (char *)&c, 1);
		else
			c = getc(stream);
		if (!internal && c == EOF) {
			if (i == 0) {
				console_echo(TRUE);
				return (-1);
			}
			getpassbuf[i] = '\0';
			break;
		}
		if ((c == '\r') || (c == '\n')) {
			getpassbuf[i] = '\0';
			if (!internal && c == '\r') {
				int c2 = getc(stream);
				if (c2 != '\n' && c2 != EOF)
					ungetc(c2, stream);
			}
			break;
		} else if (i < MAX_GETLINE) {
			getpassbuf[i++] = c;
		}
		if (i >= MAX_GETLINE) {
			getpassbuf[i] = '\0';
			break;
		}
	}

	if (linep) *linep = strdup(getpassbuf);
	if (linecapp) *linecapp = 1;

	console_echo(TRUE);

	return (i);
}

#undef getline
ssize_t
getline(char **linep, size_t *linecapp, FILE *stream)
{
	return (getline_impl(linep, linecapp,
	    stream, FALSE));
}


/* Windows POSIX wrappers */


int
wosix_fsync(int fd)
{
	if (!FlushFileBuffers(ITOH(fd)))
		return (EIO);
	return (0);
}

int
wosix_open(const char *inpath, int oflag, ...)
{
	HANDLE h;
	DWORD mode = GENERIC_READ; // RDONLY=0, WRONLY=1, RDWR=2;
	DWORD how = OPEN_EXISTING;
	DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE;
	DWORD dwFlagsAndAttributes = 0;
	char otherpath[MAXPATHLEN];
	char *path;
	char *copy_path, *r;

	copy_path = strdup(inpath);
	path = copy_path;

	/* Windows does not always handle mixed \\ and / in same path */
	r = copy_path;
	while ((r = strchr(r, '/')) != NULL)
		*r = '\\';

	/*
	 * Strip a trailing backslash: CreateFile rejects paths like
	 * "\\.\C:\dir\" and "\\server\share\" with ERROR_INVALID_NAME.
	 * Preserve drive roots ("C:\", length == 3).
	 */
	{
		size_t cplen = strlen(copy_path);
		if (cplen > 3 && copy_path[cplen - 1] == '\\')
			copy_path[cplen - 1] = '\0';
	}

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
	case (O_CREAT | O_EXCL | O_TRUNC):
		// Only creating new implies starting from 0
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
	if (oflag&O_EXLOCK) share &= ~FILE_SHARE_WRITE;
#endif

	dwFlagsAndAttributes = oflag & O_DIRECTORY ?
	    FILE_FLAG_BACKUP_SEMANTICS : FILE_ATTRIBUTE_NORMAL;

	if (oflag & O_DIRECT) {
		dwFlagsAndAttributes |=
		    FILE_FLAG_WRITE_THROUGH|FILE_FLAG_NO_BUFFERING;
	}

	// URLs have to start with "/" as in, "/C:"
	if (path[0] == '\\' && path[2] == ':')
		path++;

	// Win users might not supply \\?\ paths, make them so
	if (path[1] == ':' && strncmp("\\\\.\\", path, 4) != 0) {
		snprintf(otherpath, MAXPATHLEN, "\\\\.\\%s", &path[0]);
		path = otherpath;
	}

	// Support expansion of "SystemRoot"
	if (strncmp(path, "\\SystemRoot\\", 12) == 0) {
		snprintf(otherpath, MAXPATHLEN, "%s\\%s",
		    getenv("SystemRoot"), &path[12]);
		path = otherpath;
	}

	if (strncmp(path, "\\dev\\null", 9) == 0) {
		snprintf(otherpath, MAXPATHLEN, "NUL:");
		path = otherpath;
	}

	/*
	 * Relative path (e.g. ".") with a UNC CWD causes CreateFile to
	 * return ERROR_INVALID_NAME (123) instead of ERROR_ACCESS_DENIED,
	 * bypassing the backup-semantics retry below.  Expand to absolute
	 * using the wide-char API, which resolves correctly against a UNC CWD.
	 *
	 * Skip '#'-prefixed paths — those are the "#offset#size#device" form
	 * used for EFI partition access.  GetFullPathNameW would prepend the
	 * CWD and corrupt the encoding before the special-case handler below
	 * gets a chance to parse it.
	 */
	if (path[0] != '\\' && path[0] != '#' &&
	    !(isalpha((unsigned char)path[0]) && path[1] == ':')) {
		wchar_t wrel[MAXPATHLEN], wabs[MAXPATHLEN];
		if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wrel,
		    MAXPATHLEN) > 0 &&
		    GetFullPathNameW(wrel, MAXPATHLEN, wabs, NULL) > 0) {
			WideCharToMultiByte(CP_UTF8, 0, wabs, -1,
			    otherpath, sizeof (otherpath), NULL, NULL);
			/*
			 * GetFullPathNameW appends a trailing backslash to UNC
			 * share roots (\\server\share\).  CreateFile rejects
			 * such paths with ERROR_INVALID_NAME; strip it, but
			 * preserve drive roots like "C:\".
			 */
			size_t olen = strlen(otherpath);
			if (olen > 3 && otherpath[olen - 1] == '\\')
				otherpath[olen - 1] = '\0';
			path = otherpath;
		}
	}

	// Try to open verbatim, but if that fail, check if it is the
	// "#offset#length#name" style, and try again. We let it fail first
	// just in case someone names their file with a starting '#'.

	h = CreateFile(path, mode, share, NULL, how,
	    dwFlagsAndAttributes,
	    NULL);

	/*
	 * Directories require FILE_FLAG_BACKUP_SEMANTICS.  For UNC paths,
	 * Windows may also return ERROR_INVALID_NAME instead of
	 * ERROR_ACCESS_DENIED when BACKUP_SEMANTICS is missing.
	 */
	if (h == INVALID_HANDLE_VALUE &&
	    (GetLastError() == ERROR_ACCESS_DENIED ||
	    GetLastError() == ERROR_INVALID_NAME))
		h = CreateFile(path, mode, share, NULL, how,
		    FILE_FLAG_BACKUP_SEMANTICS,
		    NULL);

	if (h == INVALID_HANDLE_VALUE && path[0] == '#') {
		char *end = NULL;
		off_t offset;
		size_t len;
		offset = strtoull(&path[1], &end, 10);
		while (end && *end == '#') end++;
		len = strtoull(end, &end, 10);
		while (end && *end == '#') end++;

		h = CreateFile(end, mode, share, NULL, how,
		    dwFlagsAndAttributes,
		    NULL);
		if (h != INVALID_HANDLE_VALUE) {
			// Upper layer probably handles this, but let's help
			LARGE_INTEGER place;
			place.QuadPart = offset;
			SetFilePointerEx(h, place, NULL, FILE_BEGIN);
		}
	}

	// Also handle "/dev/"
	if (strncmp("/dev/", path, 5) == 0) {
		char tmp[PATH_MAX];
		snprintf(tmp, sizeof (tmp), "\\\\?\\%s", &path[5]);
		h = CreateFile(tmp, mode, share, NULL, how,
		    FILE_ATTRIBUTE_NORMAL, NULL);
	}

	// If we failed, translate error to posix
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
		case ERROR_SHARING_VIOLATION:
			errno = EBUSY; // BSD: EWOULDBLOCK
			// fall through
		default:
			fprintf(stderr, "wosix_open(%s): error %lu / 0x%lx\n",
			    path, GetLastError(), GetLastError());
		}
		free(copy_path);
		return (-1);
	}
	free(copy_path);
	return (HTOI(h));
}

int
wosix_close(int fd)
{
	HANDLE h = ITOH(fd);

	// Use CloseHandle() for everything except sockets.
	if ((GetFileType(h) == FILE_TYPE_REMOTE) &&
	    !GetNamedPipeInfo(h, NULL, NULL, NULL, NULL)) {
		int err;
		err = closesocket((SOCKET)h);
		return (err);
	}

	if (CloseHandle(h))
		return (0);
	return (-1);
}

int
wosix_ioctl_len(int fd, unsigned long request, void *wrap, size_t len)
{
	int error;
	ULONG bytesReturned;

	if (request == TIOCGWINSZ) {
		struct winsize {
			unsigned short ws_row, ws_col, ws_xpixel, ws_ypixel;
		};
		CONSOLE_SCREEN_BUFFER_INFO csbi;
		struct winsize *ws = (struct winsize *)wrap;

		if (!GetConsoleScreenBufferInfo(ITOH(fd), &csbi)) {
			errno = ENOTTY;
			return (-1);
		}

		ws->ws_col = (unsigned short)
		    (csbi.srWindow.Right - csbi.srWindow.Left + 1);
		ws->ws_row = (unsigned short)
		    (csbi.srWindow.Bottom - csbi.srWindow.Top + 1);
		ws->ws_xpixel = 0;
		ws->ws_ypixel = 0;
		return (0);
	}

	error = DeviceIoControl(ITOH(fd),
	    (DWORD)request,
	    wrap,
	    (DWORD)len,
	    wrap,
	    (DWORD)len,
	    &bytesReturned,
	    NULL);

	if (error == 0)
		error = GetLastError();
	else
		error = 0;

	errno = error;
	return (error);
}

uint64_t
wosix_lseek(int fd, uint64_t offset, int seek)
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
		return (-1);
	return (LNEW.QuadPart);
}

int
wosix_read(int fd, void *data, uint32_t len)
{
	DWORD red;
	OVERLAPPED ow = {0};

	if (GetFileType(ITOH(fd)) == FILE_TYPE_PIPE) {
		if (!ReadFile(ITOH(fd), data, len, &red, &ow))
			return (-1);
	} else {
		if (!ReadFile(ITOH(fd), data, len, &red, NULL))
			return (-1);
	}

	return (red);
}

int
wosix_write(int fd, const void *data, uint32_t len)
{
	DWORD wrote;
	OVERLAPPED ow = { 0 };

	if (GetFileType(ITOH(fd)) == FILE_TYPE_PIPE) {
		if (!WriteFile(ITOH(fd), data, len, &wrote, &ow))
			return (-1);
	} else {
		if (!WriteFile(ITOH(fd), data, len, &wrote, NULL)) {
			errno = GetLastError();
			return (-1);
		}
	}
	return (wrote);
}

ssize_t
writev(int fd, struct iovec *iov, unsigned iov_cnt)
{
	unsigned int i = 0;
	ssize_t ret = 0;
	while (i < iov_cnt) {
		ssize_t r = wosix_write(fd, iov[i].iov_base, iov[i].iov_len);

		if (r > 0) {
			ret += r;
		} else if (!r) {
			break;
		} else if (errno == EINTR) {
			continue;
		} else {
			/*
			 * else it is some "other" error,
			 * only return if there was no data processed.
			 */
			if (ret == 0) {
				ret = -1;
			}
			break;
		}
		+i++;
	}
	return (ret);
}

ssize_t
pwritev(int fd, const struct iovec *iov, int iov_cnt, off_t offset)
{
	int i = 0;
	ssize_t ret = 0;
	while (i < iov_cnt) {
		ssize_t r = pwrite64(fd, iov[i].iov_base, iov[i].iov_len,
		    offset + ret);

		if (r > 0) {
			ret += r;
		} else if (!r) {
			break;
		} else if (errno == EINTR) {
			continue;
		} else {
			/*
			 * else it is some "other" error,
			 * only return if there was no data processed.
			 */
			if (ret == 0) {
				ret = -1;
			}
			break;
		}
		i++;
	}
	return (ret);
}

int
sem_init(sem_t *sem, int pshared, unsigned int value)
{
	(void) pshared;
	sem->sem_handle = CreateSemaphore(NULL, value, LONG_MAX, NULL);
	return (sem->sem_handle != NULL ? 0 : -1);
}

int
sem_destroy(sem_t *sem)
{
	return (CloseHandle(sem->sem_handle) ? 0 : -1);
}

int
sem_post(sem_t *sem)
{
	return (ReleaseSemaphore(sem->sem_handle, 1, NULL) ? 0 : -1);
}

int
sem_wait(sem_t *sem)
{
	return (WaitForSingleObject(sem->sem_handle, INFINITE) ==
	    WAIT_OBJECT_0 ? 0 : -1);
}

int
sem_timedwait(sem_t *sem, const struct timespec *abs_timeout)
{
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);

	int64_t ms = (int64_t)(abs_timeout->tv_sec - now.tv_sec) * 1000 +
	    (abs_timeout->tv_nsec - now.tv_nsec) / 1000000;
	if (ms < 0)
		ms = 0;

	DWORD res = WaitForSingleObject(sem->sem_handle, (DWORD)ms);
	if (res == WAIT_OBJECT_0)
		return (0);
	if (res == WAIT_TIMEOUT)
		errno = ETIMEDOUT;
	return (-1);
}

ssize_t
readv(int fd, const struct iovec *iov, int iov_cnt)
{
	unsigned int i = 0;
	ssize_t ret = 0;
	while (i < iov_cnt) {
		ssize_t r = wosix_read(fd, iov[i].iov_base, iov[i].iov_len);

		if (r > 0) {
			ret += r;
		} else if (!r) {
			break;
		} else if (errno == EINTR) {
			continue;
		} else {
			/*
			 * else it is some "other" error,
			 * only return if there was no data processed.
			 */
			if (ret == 0) {
				ret = -1;
			}
			break;
		}
		+i++;
	}
	return (ret);
}

#define	is_wprefix(s, prefix) \
	(wcsncmp((s), (prefix), sizeof (prefix) / sizeof (WCHAR) - 1) == 0)

// Parts by:
// * Copyright(c) 2015 - 2017 K.Takata
// * You can redistribute it and /or modify it under the terms of either
// * the MIT license(as described below) or the Vim license.
//
// Extend isatty() slightly to return 1 for DOS Console, or
// 2 for cygwin/mingw - as we will have to do different things
// for NOECHO etc.
int
wosix_isatty(int fd)
{
	DWORD mode;
	HANDLE h = ITOH(fd);
	// int ret;

	// First, check if we are in a regular dos box, if yes, return.
	// If not, check for cygwin ...
	// check for mingw ...
	// check for powershell ...
	if (GetConsoleMode(h, &mode))
		return (1);

	// Not CMDbox, check mingw
	if (GetFileType(h) == FILE_TYPE_PIPE) {

		int size = sizeof (FILE_NAME_INFO) +
		    sizeof (WCHAR) * (MAX_PATH - 1);
		FILE_NAME_INFO* nameinfo;
		WCHAR* p = NULL;

		nameinfo = malloc(size + sizeof (WCHAR));
		if (nameinfo != NULL) {
			if (GetFileInformationByHandleEx(h, FileNameInfo,
			    nameinfo, size)) {
				nameinfo->FileName[nameinfo->FileNameLength /
				    sizeof (WCHAR)] = L'\0';
				p = nameinfo->FileName;
				if (is_wprefix(p, L"\\cygwin-")) {
					p += 8;
				} else if (is_wprefix(p, L"\\msys-")) {
					p += 6;
				} else {
					p = NULL;
				}
				if (p != NULL) {
					while (*p && isxdigit(*p))
						++p;
					if (is_wprefix(p, L"-pty")) {
						p += 4;
					} else {
						p = NULL;
					}
				}
				if (p != NULL) {
					while (*p && isdigit(*p))
						++p;
					if (is_wprefix(p, L"-from-master")) {
						// p += 12;
					} else if (is_wprefix(p,
					    L"-to-master")) {
						// p += 10;
					} else {
						p = NULL;
					}
				}
				/* ZFS elevation relay pipe is interactive */
				if (p == NULL &&
				    is_wprefix(nameinfo->FileName,
				    L"\\zfs_elev_"))
					p = nameinfo->FileName;
			}
			free(nameinfo);
			if (p != NULL)
				return (2);
		}
	}

	// Give up, it's not a TTY
	return (0);
}

// A bit different, just to wrap away the second argument
// Presumably _mkdir() sets errno, as EEXIST is tested.
int
wosix_mkdir(const char *path, mode_t mode)
{
	return (_mkdir(path));
}

int
wosix_stat(char *path, struct _stat64 *st)
{
	int fd;
	int ret;
	fd = wosix_open(path, O_RDONLY);
	if (fd == -1)
		return (-1);
	ret = wosix_fstat(fd, st);
	close(fd);
	return (ret);
}

int
wosix_lstat(char *path, struct _stat64 *st)
{
	int fd;
	int ret;

	fd = wosix_open(path, O_RDONLY);
	if (fd == -1)
		return (-1);
	ret = wosix_fstat(fd, st); // Fix me? Symlinks
	close(fd);
	return (ret);
}

// Only fill in what we actually use in ZFS
// Mostly used to test for existance, st_mode, st_size
// also FIFO and BLK (fixme)
// Remember to convert between POSIX (S_IFDIR) and WINDOWS
// (_S_IFDIR) when required.
// Not that we call Windows _stat() in here.
int
wosix_fstat(int fd, struct _stat64 *st)
{
	HANDLE h = ITOH(fd);
	BY_HANDLE_FILE_INFORMATION info;

	if (!GetFileInformationByHandle(h, &info))
		return (wosix_fstat_blk(fd, st));

	st->st_dev = 0;
	st->st_ino = 0;
	st->st_mode = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ?
	    S_IFDIR : S_IFREG;
	st->st_nlink =
	    (info.nNumberOfLinks > SHRT_MAX ? SHRT_MAX : info.nNumberOfLinks);
	st->st_uid = 0;
	st->st_gid = 0;
	st->st_rdev = 0;
	st->st_size =
	    ((long long)info.nFileSizeHigh << 32ULL) |
	    (long long)info.nFileSizeLow;
	st->st_atime = 0;
	st->st_mtime = 0;
	st->st_ctime = 0;

	return (0);
}

int
wosix_fstat_blk(int fd, struct _stat64 *st)
{
	DISK_GEOMETRY_EX geometry_ex;
	HANDLE handle = ITOH(fd);
	DWORD len;
	LARGE_INTEGER size;

	// Try device first
	if (DeviceIoControl(handle, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0,
	    &geometry_ex, sizeof (geometry_ex), &len, NULL)) {
		st->st_size = (diskaddr_t)geometry_ex.DiskSize.QuadPart;
		st->st_mode = S_IFBLK;
		return (0);
	}

	// Try regular file
	if (GetFileSizeEx(handle, &size)) {
		st->st_size = (diskaddr_t)size.QuadPart;
		st->st_mode = S_IFREG;
		return (0);
	}

	return (-1); // errno?
}

// os specific files can call this directly.
int
pread_win(HANDLE h, void *buf, size_t nbyte, off_t offset)
{
	DWORD red;
	int total_red = 0;
	LARGE_INTEGER large;
	LARGE_INTEGER lnew;
	// This code does all seeks based on "current" so we can
	// pre-seek to offset start

	// Find current position
	large.QuadPart = 0;
	SetFilePointerEx(h, large, &lnew, FILE_CURRENT);

	// Seek to place to read
	large.QuadPart = offset;
	SetFilePointerEx(h, large, NULL, FILE_BEGIN);

	boolean_t ok;

	ok = ReadFile(h, buf, nbyte, &red, NULL);

	if (!ok || red == 0)
		red = -GetLastError();

	// Restore position
	SetFilePointerEx(h, lnew, NULL, FILE_BEGIN);

	return (red);
}

int
wosix_pread(int fd, void *buf, size_t nbyte, off_t offset)
{
	return (pread_win(ITOH(fd), buf, nbyte, offset));
}

int
wosix_pwrite(int fd, const void *buf, size_t nbyte, off_t offset)
{
	HANDLE h = ITOH(fd);
	DWORD wrote;
	LARGE_INTEGER large;
	LARGE_INTEGER lnew;

	// This code does all seeks based on "current" so we can
	// pre-seek to offset start

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

	return (wrote);
}

int
wosix_fdatasync(int fd)
{
	// if (fcntl(fd, F_FULLFSYNC) == -1)
	//	return -1;
	return (0);
}

int
wosix_ftruncate(int fd, off_t length)
{
	HANDLE h = ITOH(fd);
	LARGE_INTEGER lnew;

	lnew.QuadPart = length;
	if (SetFilePointerEx(h, lnew, NULL, FILE_BEGIN) &&
	    SetEndOfFile(h))
		return (0); // Success
	// errno?
	return (-1);
}

const char *
check_file_mode(const char *mode)
{
	/* Unknown mode causes abort() */
	if (strcmp(mode, "re") == 0)
		return ("rb");
	if (strcmp(mode, "r") == 0)
		return ("rb");
	return (mode);
}

int
file_mode_fmode(const char *mode)
{
	/* Unknown mode causes abort() */
	if (strcmp(mode, "rb") == 0)
		return (O_RDONLY | O_BINARY);
	if (strcmp(mode, "r") == 0)
		return (O_RDONLY);
	if (strcmp(mode, "w") == 0)
		return (O_WRONLY);
	if (strcmp(mode, "wb") == 0)
		return (O_WRONLY | O_BINARY);
	if (strcmp(mode, "a") == 0)
		return (O_WRONLY | O_APPEND);
	if (strcmp(mode, "ab") == 0)
		return (O_WRONLY | O_BINARY | O_APPEND);
	if (strcmp(mode, "at") == 0)
		return (O_WRONLY | O_TEXT | O_APPEND);
	return (O_RDWR | O_BINARY);
}

FILE *
wosix_fopen(const char *name, const char *mode)
{
	int fd;
	int fmode = 0;
	FILE *fp;

	mode = check_file_mode(mode);

	fmode = file_mode_fmode(mode);

	// Special hack for Linux NOT using setmntent(), but
	// calling fopen(MNTTAB, directly. Boo. Let's use
	// our existing wrapper for funopen()
	if (strcmp(name, MNTTAB) == 0) {
		return (setmntent(name, mode));
	}

	// Lets enjoy the path translation work we do
	// in open.
	fd = wosix_open(name, fmode);
	if (fd < 0)
		return (NULL);

	fp = wosix_fdopen(fd, mode);

	if (fp == NULL) {
		wosix_close(fd);
		return (NULL);
	}

	fakeFILE *fFILE = malloc(sizeof (fakeFILE));
	if (!fFILE) {
		fclose(fp);
		return (NULL);
	}

	fFILE->magic = WFUNOPEN_MAGIC;
	fFILE->realFILE = fp;
	return ((FILE *)fFILE);
}

FILE *
wosix_fdopen(int fd, const char *mode)
{
	// Convert HANDLE to int
	int temp = _open_osfhandle((intptr_t)ITOH(fd), _O_APPEND | _O_RDONLY);

	if (temp == -1) {
		return (NULL);
	}

	mode = check_file_mode(mode);
	// Convert int to FILE*
	FILE *f = _fdopen(temp, mode);

	if (f == NULL) {
		_close(temp);
		return (NULL);
	}

	// fclose(f) will also call _close() on temp.
	return (f);
}

int
wosix_socketpair(int domain, int type, int protocol, int sv[2])
{
	int temp, s1, s2, result;
	struct sockaddr_in saddr;
	int nameLen;
	unsigned long option_arg = 1;
	int err = 0;

	nameLen = sizeof (saddr);

	/* ignore address family for now; just stay with AF_INET */
	temp = socket(AF_INET, SOCK_STREAM, 0);
	if (temp == INVALID_SOCKET) {
		int err = WSAGetLastError();
		errno = err;
		return (-1);
	}

	setsockopt(temp, SOL_SOCKET, SO_REUSEADDR, (void *)&option_arg,
	    sizeof (option_arg));

	/*
	 * We *SHOULD* choose the correct sockaddr structure based
	 * on the address family requested...
	 */
	memset(&saddr, 0, sizeof (saddr));

	saddr.sin_family = AF_INET;
	saddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	saddr.sin_port = 0; // give me a port

	result = bind(temp, (struct sockaddr *)&saddr, nameLen);
	if (result == SOCKET_ERROR) {
		errno = WSAGetLastError();
		closesocket(temp);
		return (-2);
	}

	// Don't care about error here, the connect will fail instead
	listen(temp, 1);

	// Fetch out the port that was given to us.
	nameLen = sizeof (struct sockaddr_in);

	result = getsockname(temp, (struct sockaddr *)&saddr, &nameLen);

	if (result == INVALID_SOCKET) {
		closesocket(temp);
		return (-4); /* error case */
	}

	s1 = socket(AF_INET, SOCK_STREAM, 0);
	if (s1 == INVALID_SOCKET) {
		closesocket(temp);
		return (-5);
	}

	nameLen = sizeof (struct sockaddr_in);

	result = connect(s1, (struct sockaddr *)&saddr, nameLen);

	if (result == INVALID_SOCKET) {
		closesocket(temp);
		closesocket(s1);
		return (-6); /* error case */
	}

	s2 = accept(temp, NULL, NULL);

	closesocket(temp);

	if (s2 == INVALID_SOCKET) {
		closesocket(s1);
		return (-7);
	}

	sv[0] = s1; sv[1] = s2;

	if ((sv[0] < 0) || (sv[1] < 0))
		return (-8);

	return (0);  /* normal case */
}

int
wosix_dup2(int fildes, int fildes2)
{
	return (0);
}

void *
wosix_mmap(void *addr, size_t len, int prot, int flags,
    int fildes, off_t off)
{
	HANDLE h = ITOH(fildes);
	HANDLE file_mapping;
	int winprot = 0, winflags = 0;
	void *mapaddr = NULL;

	/* Make a vague effort at matching flags */

	if (prot & PROT_READ) {
		winprot = PAGE_READONLY;
		winflags = FILE_MAP_READ;
	}
	if (prot & PROT_WRITE) {
		if (flags & MAP_PRIVATE)  {
			winprot = PAGE_WRITECOPY;
			winflags = FILE_MAP_COPY;
		} else if (flags & MAP_SHARED) {
			winprot = PAGE_READWRITE;
			winflags = FILE_MAP_WRITE;
		}
	}

	file_mapping = CreateFileMapping(h, NULL, winprot,
	    0, 0, NULL);

	/*
	 * PAGE_WRITECOPY requires the file handle to have been opened
	 * with GENERIC_WRITE, which read-only files (e.g. compatibility.d
	 * entries) don't have.  Fall back to VirtualAlloc + ReadFile so
	 * that MAP_PRIVATE | PROT_WRITE works on read-only file handles,
	 * matching POSIX semantics.  wosix_munmap detects which kind of
	 * region this is via VirtualQuery.
	 */
	if (file_mapping == NULL && winprot == PAGE_WRITECOPY) {
		DWORD bread = 0;
		LARGE_INTEGER lioff;
		lioff.QuadPart = off;
		mapaddr = VirtualAlloc((LPVOID)addr, len,
		    MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (mapaddr == NULL)
			return (MAP_FAILED);
		if (!SetFilePointerEx(h, lioff, NULL, FILE_BEGIN) ||
		    !ReadFile(h, mapaddr, (DWORD)len, &bread, NULL) ||
		    (size_t)bread != len) {
			VirtualFree(mapaddr, 0, MEM_RELEASE);
			return (MAP_FAILED);
		}
		return (mapaddr);
	}

	if (file_mapping == NULL)
		return (MAP_FAILED);

	mapaddr = (caddr_t)MapViewOfFileEx(file_mapping, winflags,
	    0, off, len, (LPVOID) addr);

	CloseHandle(file_mapping);
	if (mapaddr == NULL)
		return (MAP_FAILED);

	return (mapaddr);
}

int
wosix_munmap(void *addr, size_t len)
{
	MEMORY_BASIC_INFORMATION mbi;

	if (addr == NULL || addr == MAP_FAILED)
		return (0);

	if (VirtualQuery(addr, &mbi, sizeof (mbi)) &&
	    mbi.Type == MEM_PRIVATE)
		return (VirtualFree(addr, 0, MEM_RELEASE) ? 0 : -1);

	return (UnmapViewOfFile((LPVOID)addr) ? 0 : -1);
}



static uint64_t GetLogicalProcessors(void);

uint64_t
sysconf(int name)
{
	SYSTEM_INFO info;
	MEMORYSTATUSEX status;

	switch (name) {

	case _SC_NPROCESSORS_ONLN:
		return (GetLogicalProcessors());
	case _SC_IOV_MAX:
		/*
		 * pwritev()/writev() here just loop, no native scatter-
		 * gather limit; use the common Linux/glibc value.
		 */
		return (1024);
	case _SC_PHYS_PAGES:
	case _SC_PAGE_SIZE:
		GetSystemInfo(&info);
		if (name == _SC_PAGE_SIZE)
			return (info.dwPageSize);
		status.dwLength = sizeof (status);
		GlobalMemoryStatusEx(&status);
		return ((long)(status.ullTotalPhys / info.dwPageSize));
	default:
		return (-1);
	}
}


typedef BOOL(WINAPI *LPFN_GLPI)(PSYSTEM_LOGICAL_PROCESSOR_INFORMATION,
    PDWORD);

// Helper function to count set bits in the processor mask.
static DWORD
CountSetBits(ULONG_PTR bitMask)
{
	DWORD LSHIFT = sizeof (ULONG_PTR)*8 - 1;
	DWORD bitSetCount = 0;
	ULONG_PTR bitTest = (ULONG_PTR)1 << LSHIFT;
	DWORD i;

	for (i = 0; i <= LSHIFT; ++i) {
		bitSetCount += ((bitMask & bitTest)?1:0);
		bitTest /= 2;
	}

	return (bitSetCount);
}

static uint64_t
GetLogicalProcessors(void)
{
	LPFN_GLPI glpi;
	BOOL done = FALSE;
	PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer = NULL;
	PSYSTEM_LOGICAL_PROCESSOR_INFORMATION ptr = NULL;
	DWORD returnLength = 0;
	uint64_t logicalProcessorCount = 0;
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
		return (0);

	while (!done) {
		DWORD rc = glpi(buffer, &returnLength);

		if (FALSE == rc) {
			if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
				if (buffer)
					free(buffer);

				buffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)
				    malloc(returnLength);

				if (NULL == buffer)
					return (0);
			} else {
				return (0);
			}
		} else {
			done = TRUE;
		}
	}

	ptr = buffer;

	while (byteOffset + sizeof (SYSTEM_LOGICAL_PROCESSOR_INFORMATION) <=
	    returnLength) {
		switch (ptr->Relationship) {
			case RelationNumaNode:
			// Non-NUMA systems report a single record of this type.
			numaNodeCount++;
			break;

		case RelationProcessorCore:
			processorCoreCount++;

			// A hyperthreaded core supplies more than one
			// logical processor.
			logicalProcessorCount +=
			    CountSetBits(ptr->ProcessorMask);
			break;

		case RelationCache:
			// Cache data is in ptr->Cache, one CACHE_DESCRIPTOR
			// structure for each cache.
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
	byteOffset += sizeof (SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
	ptr++;
	}

	free(buffer);
	return (logicalProcessorCount);
}

int
mprotect(void *addr, size_t len, int prot)
{
	// We can probably implement something using VirtualProtect() here.
	return (0);
}

uid_t
getuid(void)
{
	return (1);
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
	// GetVersionEx(&versionex);
	VerifyVersionInfo(&versionex, VER_MAJORVERSION | VER_MINORVERSION, 0);
	snprintf(buf->sysname, sizeof (buf->sysname), "Windows_NT-%u.%u",
	    (unsigned int) versionex.dwMajorVersion,
	    (unsigned int) versionex.dwMinorVersion);

	GetSystemInfo(&info);

	switch (info.wProcessorArchitecture) {
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
	switch (item) {
	/* nl_langinfo items of the LC_CTYPE category */
	case _DATE_FMT:
		return ("%y/%m/%d");
	}
	return ("");
}

int
wosix_openat(int fd, const char *path, int oflag, ...)
{
	HANDLE h = ITOH(fd);
	char fullpath[MAXPATHLEN];

	if (fd == AT_FDCWD)
		return (wosix_open(path, oflag));

	/* Absolute path: ignore dirfd, same as POSIX */
	if (path[0] == '/' || (path[0] != '\0' && path[1] == ':'))
		return (wosix_open(path, oflag));

	/*
	 * Fetch the directory name, and stitch the name together.
	 * Another option is using NTCreateFile with RootDirectory=handle
	 */

	DWORD gplen = GetFinalPathNameByHandleA(h, fullpath,
	    MAXPATHLEN, FILE_NAME_NORMALIZED);
	if (gplen > 0) {
		strlcat(fullpath, "\\", MAXPATHLEN);
		strlcat(fullpath, path, MAXPATHLEN);
		return (wosix_open(fullpath, oflag));
	}
	return (-1);
}

/*
 * This is a poor "port" of freopen() but, to date, it is only
 * used to re-open the MNTTAB, of which we have none, and the return
 * code is never used.
 */
FILE *
wosix_freopen(const char *path, const char *mode, FILE *stream)
{
	return ((FILE *)path); // Anything not NULL
}

int
timer_create(clockid_t id, struct sigevent *__restrict se,
    timer_t *__restrict t)
{
	return (0);
}

int
timer_delete(timer_t t)
{
	return (0);
}

int
timer_gettime(timer_t t, struct itimerspec *v)
{
	return (0);
}

int
timer_getoverrun(timer_t t)
{
	return (0);
}

int
timer_settime(timer_t t, int x, const struct itimerspec *tv,
    struct itimerspec *itv)
{
	return (0);
}

int
wosix_access(const char *name, int mode)
{
	DWORD dwAttrib = GetFileAttributes(name);
	boolean_t isFile;

	isFile = (dwAttrib != INVALID_FILE_ATTRIBUTES &&
	    !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));

	if (!isFile) {
		errno = ENOENT;
		return (-1);
	}

	/* Windows does not have X_OK (execute), and it assert()s */
	if (mode == X_OK)
		return (0);

	mode &= ~X_OK;

#undef access
	return (access(name, mode));
}

char *
strptime(const char *s,
    const char *f,
    struct tm *tm)
{
	/* This desperately needs implementing */
	(void) tm;
	return (s);
}

int
getpwnam_r(const char *name, struct passwd *pwd,
    char *buf, size_t buflen, struct passwd **result)
{
	*result = NULL;
	return (0);
}

int
getgrnam_r(const char *name, struct group *grp,
    char *buf, size_t buflen, struct group **result)
{
	*result = NULL;
	return (0);
}

extern pid_t setsid(void)
{
	return (0);
}

/*
 * hcreate()/hsearch()/hdestroy() - POSIX only requires one table open
 * at a time, which matches how zstream's decompress/drop_record use it:
 * build a small table keyed by "object,offset" strings, look entries up
 * while streaming, then tear it down.  Keys are stored by reference (not
 * copied), matching traditional hsearch() behaviour - callers must keep
 * the key storage alive for as long as the table exists.
 */
typedef struct hsearch_node {
	ENTRY item;
	struct hsearch_node *next;
} hsearch_node_t;

static hsearch_node_t **hsearch_tab = NULL;
static size_t hsearch_tab_size = 0;

static size_t
hsearch_hash(const char *key)
{
	size_t h = 5381;
	while (*key != '\0')
		h = h * 33 + (unsigned char)*key++;
	return (h);
}

int
hcreate(size_t nel)
{
	if (hsearch_tab != NULL)
		hdestroy();

	hsearch_tab_size = nel < 8 ? 8 : nel * 2;
	hsearch_tab = calloc(hsearch_tab_size, sizeof (hsearch_node_t *));
	if (hsearch_tab == NULL) {
		hsearch_tab_size = 0;
		return (0);
	}
	return (1);
}

void
hdestroy(void)
{
	if (hsearch_tab == NULL)
		return;

	for (size_t i = 0; i < hsearch_tab_size; i++) {
		hsearch_node_t *node = hsearch_tab[i];
		while (node != NULL) {
			hsearch_node_t *next = node->next;
			free(node);
			node = next;
		}
	}
	free(hsearch_tab);
	hsearch_tab = NULL;
	hsearch_tab_size = 0;
}

ENTRY *
hsearch(ENTRY item, ACTION action)
{
	if (hsearch_tab == NULL)
		return (NULL);

	size_t idx = hsearch_hash(item.key) % hsearch_tab_size;
	for (hsearch_node_t *node = hsearch_tab[idx]; node != NULL;
	    node = node->next) {
		if (strcmp(node->item.key, item.key) == 0)
			return (&node->item);
	}

	if (action == FIND)
		return (NULL);

	hsearch_node_t *node = malloc(sizeof (hsearch_node_t));
	if (node == NULL)
		return (NULL);
	node->item = item;
	node->next = hsearch_tab[idx];
	hsearch_tab[idx] = node;
	return (&node->item);
}

/*
 * Build a Windows command line string from a NULL-terminated argv[],
 * quoting/escaping each argument per the rules CommandLineToArgvW (and
 * CRT startup code) expect.  Caller frees the result.
 */
static char *
argv_to_cmdline(char *argv[])
{
	size_t cap = 1;
	for (int i = 0; argv[i] != NULL; i++)
		cap += strlen(argv[i]) * 2 + 3;

	char *cmd = malloc(cap);
	if (cmd == NULL)
		return (NULL);

	size_t pos = 0;
	for (int i = 0; argv[i] != NULL; i++) {
		const char *arg = argv[i];
		boolean_t quote = arg[0] == '\0' ||
		    strpbrk(arg, " \t\"") != NULL;

		if (i > 0)
			cmd[pos++] = ' ';

		if (!quote) {
			size_t alen = strlen(arg);
			memcpy(cmd + pos, arg, alen);
			pos += alen;
			continue;
		}

		cmd[pos++] = '"';
		for (const char *p = arg; *p != '\0'; ) {
			size_t nbs = 0;
			while (*p == '\\') {
				nbs++;
				p++;
			}
			if (*p == '\0') {
				for (size_t j = 0; j < nbs * 2; j++)
					cmd[pos++] = '\\';
				break;
			}
			size_t rep = (*p == '"') ? nbs * 2 + 1 : nbs;
			for (size_t j = 0; j < rep; j++)
				cmd[pos++] = '\\';
			cmd[pos++] = *p++;
		}
		cmd[pos++] = '"';
	}
	cmd[pos] = '\0';
	return (cmd);
}

/*
 * Build a Windows double-NUL-terminated environment block from a
 * NULL-terminated array of "NAME=VALUE" strings.  This is an exact
 * replacement block (no merging with the calling process's own
 * environment), matching execve()/execvpe()'s semantics on other
 * platforms.  Caller frees the result.
 */
static char *
env_array_to_block(char *env[])
{
	size_t len = 1;
	int n;
	for (n = 0; env[n] != NULL; n++)
		len += strlen(env[n]) + 1;

	char *block = malloc(len);
	if (block == NULL)
		return (NULL);

	size_t pos = 0;
	for (int i = 0; i < n; i++) {
		size_t l = strlen(env[i]) + 1;
		memcpy(block + pos, env[i], l);
		pos += l;
	}
	block[pos] = '\0';
	return (block);
}

/*
 * Run argv[0] with the given argv/env, wait for it to exit, and return
 * its exit code (or -1 if the process itself could not be created).
 *
 * env, when non-NULL, entirely replaces the child's environment (rather
 * than being merged with ours), matching execve()/execvpe() on other
 * platforms - the caller is expected to build a complete environment.
 * When NULL, the child inherits our environment, matching execv()/
 * execvp().
 *
 * If capture_stdout_fd is non-NULL, the child's stdout is captured
 * through a pipe and a CRT file descriptor for the read end is handed
 * back through it for the caller to read (e.g. with fdopen()) and
 * close; otherwise stdout follows stdout_verbose (inherited if true,
 * discarded otherwise).  stderr always just follows stderr_verbose.
 */
int
wosix_run_process(char *argv[], char *env[], boolean_t stdout_verbose,
    boolean_t stderr_verbose, int *capture_stdout_fd)
{
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	HANDLE pipe_read = NULL, pipe_write = NULL;
	HANDLE stdout_devnull = NULL, stderr_devnull = NULL;
	char *cmdline, *envblock = NULL;
	DWORD exit_code = (DWORD)-1;

	cmdline = argv_to_cmdline(argv);
	if (cmdline == NULL)
		return (-1);

	if (env != NULL) {
		envblock = env_array_to_block(env);
		if (envblock == NULL) {
			free(cmdline);
			return (-1);
		}
	}

	ZeroMemory(&si, sizeof (si));
	si.cb = sizeof (si);
	ZeroMemory(&pi, sizeof (pi));
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

	if (capture_stdout_fd != NULL) {
		SECURITY_ATTRIBUTES sa;
		ZeroMemory(&sa, sizeof (sa));
		sa.nLength = sizeof (sa);
		sa.bInheritHandle = TRUE;
		if (!CreatePipe(&pipe_read, &pipe_write, &sa, 0))
			goto out;
		SetHandleInformation(pipe_read, HANDLE_FLAG_INHERIT, 0);
		si.hStdOutput = pipe_write;
	} else if (stdout_verbose) {
		si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	} else {
		stdout_devnull = CreateFile("NUL", GENERIC_WRITE,
		    FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
		    OPEN_EXISTING, 0, NULL);
		si.hStdOutput = stdout_devnull;
	}

	if (stderr_verbose) {
		si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
	} else {
		stderr_devnull = CreateFile("NUL", GENERIC_WRITE,
		    FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
		    OPEN_EXISTING, 0, NULL);
		si.hStdError = stderr_devnull;
	}

	if (!CreateProcess(NULL, cmdline, NULL, NULL, TRUE, 0,
	    envblock, NULL, &si, &pi))
		goto out;

	CloseHandle(pi.hThread);
	if (pipe_write != NULL) {
		CloseHandle(pipe_write);
		pipe_write = NULL;
	}

	if (capture_stdout_fd != NULL) {
		/*
		 * Callers pass this to the shared (cross-platform)
		 * libzfs_read_stdout_from_fd(), which calls fdopen() -
		 * wosix_fdopen() expects an HTOI()-encoded HANDLE (this
		 * codebase's own "fd" convention), not a real CRT fd from
		 * _open_osfhandle(); it would ITOH() it right back into
		 * garbage. Hand back the raw handle in that encoding and
		 * let wosix_fdopen() do the _open_osfhandle() itself.
		 */
		*capture_stdout_fd = HTOI(pipe_read);
		pipe_read = NULL; /* ownership transfers via wosix_fdopen() */
	}

	WaitForSingleObject(pi.hProcess, INFINITE);
	GetExitCodeProcess(pi.hProcess, &exit_code);
	CloseHandle(pi.hProcess);

out:
	if (pipe_read != NULL)
		CloseHandle(pipe_read);
	if (pipe_write != NULL)
		CloseHandle(pipe_write);
	if (stdout_devnull != NULL)
		CloseHandle(stdout_devnull);
	if (stderr_devnull != NULL)
		CloseHandle(stderr_devnull);
	free(cmdline);
	free(envblock);

	return (exit_code == (DWORD)-1 ? -1 : (int)exit_code);
}
