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

/*
 *
 * Copyright (C) 2017 Jorgen Lundman <lundman@lundman.net>
 *
 */
#ifndef _SPL_TYPES_H
#define	_SPL_TYPES_H


// Linux kernel optimization, ignore them for now on OSX.
#define unlikely
#define likely
// use ntintsafe.h ?
typedef enum { B_FALSE = 0, B_TRUE = 1 }	boolean_t;
typedef short				pri_t;
typedef int int32_t;
typedef unsigned long			ulong;
typedef unsigned long			ulong_t;
typedef unsigned long long		uint64_t;
typedef unsigned long long		u_longlong_t;
typedef unsigned long long		rlim64_t;
typedef unsigned long long		loff_t;
#define _CLOCK_T_DEFINED
typedef unsigned long long		clock_t;
typedef long long				int64_t;
typedef long long			longlong_t;
typedef unsigned char			uchar_t;
typedef unsigned int			uint_t;
typedef unsigned int			uint32_t;
typedef unsigned short			ushort_t;
typedef void *spinlock_t;
typedef long long			offset_t;
typedef long long			off_t;
typedef struct timespec			timestruc_t; /* definition per SVr4 */
typedef struct timespec			timespec_t;
typedef ulong_t				pgcnt_t;
typedef unsigned int mode_t ;
//typedef   uint32_t        dev32_t;
#define   NODEV32 (dev32_t)(-1)
typedef uint_t				minor_t;
typedef char *caddr_t;
typedef unsigned char uint8_t;
typedef char int8_t;
typedef short int int16_t;
typedef unsigned short int uint16_t;
//#include_next <sys/types.h>
#include <string.h>
#include <sys/sysmacros.h>
//#include <libkern/libkern.h>
typedef unsigned long long uid_t;
typedef unsigned long long gid_t;
typedef unsigned int pid_t;
// size_t is 32bit on IllumOS, but 64bit on windows, so changed to uint32_t
typedef uintptr_t pc_t;
typedef uint64_t ssize_t;
typedef uint64_t vm_offset_t;
typedef uint64_t dev_t;
#define NGROUPS 16

typedef unsigned short umode_t;
typedef uint64_t               user_addr_t;
typedef uint64_t user_size_t;
typedef uint64_t               ino64_t;

typedef unsigned long u_long;
typedef unsigned char   uuid_t[16];


// Yeah nothing is going to work until we fix this atomic stuff
#define _Atomic

#define PATH_MAX                 1024 
#define Z_OK 0

struct buf;
typedef struct buf buf_t;
typedef unsigned int uInt;
#include <sys/stropts.h>
#include <sys/errno.h>
#include <ntintsafe.h>
#include <ntstrsafe.h>
#include <stdlib.h>
#include <ntddk.h>


#define snprintf _snprintf
#define vprintf(...) vKdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, __VA_ARGS__))
#define vsnprintf _vsnprintf

#ifndef ULLONG_MAX
#define ULLONG_MAX			(~0ULL)
#endif

#ifndef LLONG_MAX
#define LLONG_MAX			((long long)(~0ULL>>1))
#endif

#if 0
typedef unsigned long			intptr_t;
typedef unsigned long long		u_offset_t;
typedef struct task_struct		kthread_t;
typedef struct task_struct		proc_t;
typedef struct vmem { }			vmem_t;
typedef struct timespec			timestruc_t; /* definition per SVr4 */
typedef struct timespec			timespec_t;
typedef u_longlong_t			len_t;
typedef longlong_t			diskaddr_t;
typedef ushort_t			o_bcopy;
typedef uint_t				major_t;
typedef ulong_t				pfn_t;
typedef long				spgcnt_t;
typedef short				index_t;
typedef int				id_t;
typedef unsigned short mode_t;
extern proc_t p0;
#endif

#include  <sys/fcntl.h>
#define FREAD           0x0001
#define FWRITE          0x0002

#define FCREAT          O_CREAT
#define FTRUNC          O_TRUNC
#define FEXCL           O_EXCL
#define FNOCTTY         O_NOCTTY
//#define       FASYNC          O_SYNC
#define FNOFOLLOW       O_NOFOLLOW
#define FAPPEND         O_APPEND   


#define FSYNC           0x10    /* file (data+inode) integrity while writing */
#define FDSYNC          0x40    /* file data only integrity while writing */
#define FRSYNC          0x8000  /* sync read operations at same level of */
                                /* integrity as specified for writes by */
                                /* FSYNC and FDSYNC flags */
#define FOFFMAX         0x2000  /* large file */

#define EXPORT_SYMBOL(X)
#define module_param(X,Y,Z)
#define MODULE_PARM_DESC(X,Y)

#define B_WRITE         0x00000000      /* Write buffer (pseudo flag). */
#define B_READ          0x00000001      /* Read buffer. */
#define B_ASYNC         0x00000002      /* Start I/O, do not wait. */
#define B_NOCACHE       0x00000004      /* Do not cache block after use. */
#define B_PHYS          0x00000020      /* I/O to user memory. */
#define B_PASSIVE       0x00000800      /* PASSIVE I/Os are ignored by THROTTLE I/O */
#define	B_BUSY	B_PHYS

#ifdef __GNUC__
#define member_type(type, member) __typeof__ (((type *)0)->member)
#else
#define member_type(type, member) void
#endif

#define container_of(ptr, type, member) ((type *)(                      \
      (char *)(member_type(type, member) *){ ptr } - offsetof(type, member)))

#define bzero(b,len) (memset((b), '\0', (len)))
#define bcopy(b1,b2,len) (memmove((b2), (b1), (len)))
#define bcmp(b1, b2, len) (memcmp((b2), (b1), (len)))
//int snprintf(char *s, int l, char *fmt, ...);

extern uint32_t strlcpy(register char* s, register const char* t, register uint32_t n);
extern uint32_t strlcat(register char* s, register const char* t, register uint32_t n);
#define strtok_r strtok_s
#define strcasecmp _stricmp

#define htonl _byteswap_ulong

struct mount;
typedef struct mount mount_t;

#define always_inline __forceinline
#define __attribute__

struct kauth_cred;
typedef struct kauth_cred kauth_cred_t;
struct kauth_acl;
typedef struct kauth_acl kauth_acl_t;
#define KAUTH_FILESEC_NONE      ((kauth_filesec_t)0)

struct kauth_ace_rights;
typedef struct kauth_ace_rights kauth_ace_rights_t;

extern int      groupmember(gid_t gid, kauth_cred_t *cred);

typedef struct {
#define KAUTH_GUID_SIZE 16      /* 128-bit identifier */
	unsigned char g_guid[KAUTH_GUID_SIZE];
} guid_t;

#pragma warning( disable: 4296 )  // expression is always true
#pragma error( disable: 4296 )  // expression is always true
#pragma warning( disable: 4703 )  // potentially uninitialized local pointer variable

#define LINK_MAX                32767   /* max file link count */

#define FNV1_32A_INIT ((uint32_t)0x811c9dc5)
uint32_t fnv_32a_str(const char *str, uint32_t hval);
uint32_t fnv_32a_buf(void *buf, size_t len, uint32_t hval);

#endif	/* _SPL_TYPES_H */
