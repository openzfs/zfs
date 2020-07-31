#ifndef _SPL_FCNTL_H
#define _SPL_FCNTL_H

#include <sys/types.h>
#include <fcntl.h>

#define F_FREESP 11

#define F_RDLCK         1               /* shared or read lock */
#define F_UNLCK         2               /* unlock */
#define F_WRLCK         3               /* exclusive or write lock */
#ifdef KERNEL
#define F_WAIT          0x010           /* Wait until lock is granted */
#define F_FLOCK         0x020           /* Use flock(2) semantics for lock */
#define F_POSIX         0x040           /* Use POSIX semantics for lock */
#define F_PROV          0x080           /* Non-coalesced provisional lock */
#define F_WAKE1_SAFE    0x100           /* its safe to only wake one waiter */
#define F_ABORT         0x200           /* lock attempt aborted (force umount) */
#define F_OFD_LOCK      0x400           /* Use "OFD" semantics for lock */
#endif

struct flock {
	off_t   l_start;        /* starting offset */
	off_t   l_len;          /* len = 0 means until end of file */
	pid_t   l_pid;          /* lock owner */
	short   l_type;         /* lock type: read/write, etc. */
	short   l_whence;       /* type of l_start */
};

#endif /* _SPL_FCNTL_H */