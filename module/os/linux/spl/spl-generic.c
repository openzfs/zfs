/*
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  Solaris Porting Layer (SPL) Generic Implementation.
 */

#include <sys/isa_defs.h>
#include <sys/sysmacros.h>
#include <sys/systeminfo.h>
#include <sys/vmsystm.h>
#include <sys/kmem.h>
#include <sys/kmem_cache.h>
#include <sys/vmem.h>
#include <sys/mutex.h>
#include <sys/rwlock.h>
#include <sys/taskq.h>
#include <sys/tsd.h>
#include <sys/zmod.h>
#include <sys/debug.h>
#include <sys/proc.h>
#include <sys/kstat.h>
#include <sys/file.h>
#include <sys/sunddi.h>
#include <linux/ctype.h>
#include <sys/disp.h>
#include <sys/random.h>
#include <sys/string.h>
#include <linux/kmod.h>
#include <linux/mod_compat.h>
#include <sys/cred.h>
#include <sys/vnode.h>
#include <sys/misc.h>
#include <linux/mod_compat.h>

unsigned long spl_hostid = 0;
EXPORT_SYMBOL(spl_hostid);

/* CSTYLED */
module_param(spl_hostid, ulong, 0644);
MODULE_PARM_DESC(spl_hostid, "The system hostid.");

proc_t p0;
EXPORT_SYMBOL(p0);

/*
 * xoshiro256++ 1.0 PRNG by David Blackman and Sebastiano Vigna
 *
 * "Scrambled Linear Pseudorandom Number Generators∗"
 * https://vigna.di.unimi.it/ftp/papers/ScrambledLinear.pdf
 *
 * random_get_pseudo_bytes() is an API function on Illumos whose sole purpose
 * is to provide bytes containing random numbers. It is mapped to /dev/urandom
 * on Illumos, which uses a "FIPS 186-2 algorithm". No user of the SPL's
 * random_get_pseudo_bytes() needs bytes that are of cryptographic quality, so
 * we can implement it using a fast PRNG that we seed using Linux' actual
 * equivalent to random_get_pseudo_bytes(). We do this by providing each CPU
 * with an independent seed so that all calls to random_get_pseudo_bytes() are
 * free of atomic instructions.
 *
 * A consequence of using a fast PRNG is that using random_get_pseudo_bytes()
 * to generate words larger than 256 bits will paradoxically be limited to
 * `2^256 - 1` possibilities. This is because we have a sequence of `2^256 - 1`
 * 256-bit words and selecting the first will implicitly select the second. If
 * a caller finds this behavior undesirable, random_get_bytes() should be used
 * instead.
 *
 * XXX: Linux interrupt handlers that trigger within the critical section
 * formed by `s[3] = xp[3];` and `xp[0] = s[0];` and call this function will
 * see the same numbers. Nothing in the code currently calls this in an
 * interrupt handler, so this is considered to be okay. If that becomes a
 * problem, we could create a set of per-cpu variables for interrupt handlers
 * and use them when in_interrupt() from linux/preempt_mask.h evaluates to
 * true.
 */
static void __percpu *spl_pseudo_entropy;

/*
 * rotl()/spl_rand_next()/spl_rand_jump() are copied from the following CC-0
 * licensed file:
 *
 * https://prng.di.unimi.it/xoshiro256plusplus.c
 */

static inline uint64_t rotl(const uint64_t x, int k)
{
	return ((x << k) | (x >> (64 - k)));
}

static inline uint64_t
spl_rand_next(uint64_t *s)
{
	const uint64_t result = rotl(s[0] + s[3], 23) + s[0];

	const uint64_t t = s[1] << 17;

	s[2] ^= s[0];
	s[3] ^= s[1];
	s[1] ^= s[2];
	s[0] ^= s[3];

	s[2] ^= t;

	s[3] = rotl(s[3], 45);

	return (result);
}

static inline void
spl_rand_jump(uint64_t *s)
{
	static const uint64_t JUMP[] = { 0x180ec6d33cfd0aba,
	    0xd5a61266f0c9392c, 0xa9582618e03fc9aa, 0x39abdc4529b1661c };

	uint64_t s0 = 0;
	uint64_t s1 = 0;
	uint64_t s2 = 0;
	uint64_t s3 = 0;
	int i, b;
	for (i = 0; i < sizeof (JUMP) / sizeof (*JUMP); i++)
		for (b = 0; b < 64; b++) {
			if (JUMP[i] & 1ULL << b) {
				s0 ^= s[0];
				s1 ^= s[1];
				s2 ^= s[2];
				s3 ^= s[3];
			}
			(void) spl_rand_next(s);
		}

	s[0] = s0;
	s[1] = s1;
	s[2] = s2;
	s[3] = s3;
}

int
random_get_pseudo_bytes(uint8_t *ptr, size_t len)
{
	uint64_t *xp, s[4];

	ASSERT(ptr);

	xp = get_cpu_ptr(spl_pseudo_entropy);

	s[0] = xp[0];
	s[1] = xp[1];
	s[2] = xp[2];
	s[3] = xp[3];

	while (len) {
		union {
			uint64_t ui64;
			uint8_t byte[sizeof (uint64_t)];
		}entropy;
		int i = MIN(len, sizeof (uint64_t));

		len -= i;
		entropy.ui64 = spl_rand_next(s);

		/*
		 * xoshiro256++ has low entropy lower bytes, so we copy the
		 * higher order bytes first.
		 */
		while (i--)
#ifdef _ZFS_BIG_ENDIAN
			*ptr++ = entropy.byte[i];
#else
			*ptr++ = entropy.byte[7 - i];
#endif
	}

	xp[0] = s[0];
	xp[1] = s[1];
	xp[2] = s[2];
	xp[3] = s[3];

	put_cpu_ptr(spl_pseudo_entropy);

	return (0);
}


EXPORT_SYMBOL(random_get_pseudo_bytes);

#if BITS_PER_LONG == 32

/*
 * Support 64/64 => 64 division on a 32-bit platform.  While the kernel
 * provides a div64_u64() function for this we do not use it because the
 * implementation is flawed.  There are cases which return incorrect
 * results as late as linux-2.6.35.  Until this is fixed upstream the
 * spl must provide its own implementation.
 *
 * This implementation is a slightly modified version of the algorithm
 * proposed by the book 'Hacker's Delight'.  The original source can be
 * found here and is available for use without restriction.
 *
 * http://www.hackersdelight.org/HDcode/newCode/divDouble.c
 */

/*
 * Calculate number of leading of zeros for a 64-bit value.
 */
static int
nlz64(uint64_t x)
{
	register int n = 0;

	if (x == 0)
		return (64);

	if (x <= 0x00000000FFFFFFFFULL) { n = n + 32; x = x << 32; }
	if (x <= 0x0000FFFFFFFFFFFFULL) { n = n + 16; x = x << 16; }
	if (x <= 0x00FFFFFFFFFFFFFFULL) { n = n +  8; x = x <<  8; }
	if (x <= 0x0FFFFFFFFFFFFFFFULL) { n = n +  4; x = x <<  4; }
	if (x <= 0x3FFFFFFFFFFFFFFFULL) { n = n +  2; x = x <<  2; }
	if (x <= 0x7FFFFFFFFFFFFFFFULL) { n = n +  1; }

	return (n);
}

/*
 * Newer kernels have a div_u64() function but we define our own
 * to simplify portability between kernel versions.
 */
static inline uint64_t
__div_u64(uint64_t u, uint32_t v)
{
	(void) do_div(u, v);
	return (u);
}

/*
 * Turn off missing prototypes warning for these functions. They are
 * replacements for libgcc-provided functions and will never be called
 * directly.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-prototypes"

/*
 * Implementation of 64-bit unsigned division for 32-bit machines.
 *
 * First the procedure takes care of the case in which the divisor is a
 * 32-bit quantity. There are two subcases: (1) If the left half of the
 * dividend is less than the divisor, one execution of do_div() is all that
 * is required (overflow is not possible). (2) Otherwise it does two
 * divisions, using the grade school method.
 */
uint64_t
__udivdi3(uint64_t u, uint64_t v)
{
	uint64_t u0, u1, v1, q0, q1, k;
	int n;

	if (v >> 32 == 0) {			// If v < 2**32:
		if (u >> 32 < v) {		// If u/v cannot overflow,
			return (__div_u64(u, v)); // just do one division.
		} else {			// If u/v would overflow:
			u1 = u >> 32;		// Break u into two halves.
			u0 = u & 0xFFFFFFFF;
			q1 = __div_u64(u1, v);	// First quotient digit.
			k  = u1 - q1 * v;	// First remainder, < v.
			u0 += (k << 32);
			q0 = __div_u64(u0, v);	// Seconds quotient digit.
			return ((q1 << 32) + q0);
		}
	} else {				// If v >= 2**32:
		n = nlz64(v);			// 0 <= n <= 31.
		v1 = (v << n) >> 32;		// Normalize divisor, MSB is 1.
		u1 = u >> 1;			// To ensure no overflow.
		q1 = __div_u64(u1, v1);		// Get quotient from
		q0 = (q1 << n) >> 31;		// Undo normalization and
						// division of u by 2.
		if (q0 != 0)			// Make q0 correct or
			q0 = q0 - 1;		// too small by 1.
		if ((u - q0 * v) >= v)
			q0 = q0 + 1;		// Now q0 is correct.

		return (q0);
	}
}
EXPORT_SYMBOL(__udivdi3);

#ifndef abs64
/* CSTYLED */
#define	abs64(x)	({ uint64_t t = (x) >> 63; ((x) ^ t) - t; })
#endif

/*
 * Implementation of 64-bit signed division for 32-bit machines.
 */
int64_t
__divdi3(int64_t u, int64_t v)
{
	int64_t q, t;
	q = __udivdi3(abs64(u), abs64(v));
	t = (u ^ v) >> 63;	// If u, v have different
	return ((q ^ t) - t);	// signs, negate q.
}
EXPORT_SYMBOL(__divdi3);

/*
 * Implementation of 64-bit unsigned modulo for 32-bit machines.
 */
uint64_t
__umoddi3(uint64_t dividend, uint64_t divisor)
{
	return (dividend - (divisor * __udivdi3(dividend, divisor)));
}
EXPORT_SYMBOL(__umoddi3);

/* 64-bit signed modulo for 32-bit machines. */
int64_t
__moddi3(int64_t n, int64_t d)
{
	int64_t q;
	boolean_t nn = B_FALSE;

	if (n < 0) {
		nn = B_TRUE;
		n = -n;
	}
	if (d < 0)
		d = -d;

	q = __umoddi3(n, d);

	return (nn ? -q : q);
}
EXPORT_SYMBOL(__moddi3);

/*
 * Implementation of 64-bit unsigned division/modulo for 32-bit machines.
 */
uint64_t
__udivmoddi4(uint64_t n, uint64_t d, uint64_t *r)
{
	uint64_t q = __udivdi3(n, d);
	if (r)
		*r = n - d * q;
	return (q);
}
EXPORT_SYMBOL(__udivmoddi4);

/*
 * Implementation of 64-bit signed division/modulo for 32-bit machines.
 */
int64_t
__divmoddi4(int64_t n, int64_t d, int64_t *r)
{
	int64_t q, rr;
	boolean_t nn = B_FALSE;
	boolean_t nd = B_FALSE;
	if (n < 0) {
		nn = B_TRUE;
		n = -n;
	}
	if (d < 0) {
		nd = B_TRUE;
		d = -d;
	}

	q = __udivmoddi4(n, d, (uint64_t *)&rr);

	if (nn != nd)
		q = -q;
	if (nn)
		rr = -rr;
	if (r)
		*r = rr;
	return (q);
}
EXPORT_SYMBOL(__divmoddi4);

#if defined(__arm) || defined(__arm__)
/*
 * Implementation of 64-bit (un)signed division for 32-bit arm machines.
 *
 * Run-time ABI for the ARM Architecture (page 20).  A pair of (unsigned)
 * long longs is returned in {{r0, r1}, {r2,r3}}, the quotient in {r0, r1},
 * and the remainder in {r2, r3}.  The return type is specifically left
 * set to 'void' to ensure the compiler does not overwrite these registers
 * during the return.  All results are in registers as per ABI
 */
void
__aeabi_uldivmod(uint64_t u, uint64_t v)
{
	uint64_t res;
	uint64_t mod;

	res = __udivdi3(u, v);
	mod = __umoddi3(u, v);
	{
		register uint32_t r0 asm("r0") = (res & 0xFFFFFFFF);
		register uint32_t r1 asm("r1") = (res >> 32);
		register uint32_t r2 asm("r2") = (mod & 0xFFFFFFFF);
		register uint32_t r3 asm("r3") = (mod >> 32);

		asm volatile(""
		    : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3)  /* output */
		    : "r"(r0), "r"(r1), "r"(r2), "r"(r3));    /* input */

		return; /* r0; */
	}
}
EXPORT_SYMBOL(__aeabi_uldivmod);

void
__aeabi_ldivmod(int64_t u, int64_t v)
{
	int64_t res;
	uint64_t mod;

	res =  __divdi3(u, v);
	mod = __umoddi3(u, v);
	{
		register uint32_t r0 asm("r0") = (res & 0xFFFFFFFF);
		register uint32_t r1 asm("r1") = (res >> 32);
		register uint32_t r2 asm("r2") = (mod & 0xFFFFFFFF);
		register uint32_t r3 asm("r3") = (mod >> 32);

		asm volatile(""
		    : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3)  /* output */
		    : "r"(r0), "r"(r1), "r"(r2), "r"(r3));    /* input */

		return; /* r0; */
	}
}
EXPORT_SYMBOL(__aeabi_ldivmod);
#endif /* __arm || __arm__ */

#pragma GCC diagnostic pop

#endif /* BITS_PER_LONG */

/*
 * NOTE: The strtoxx behavior is solely based on my reading of the Solaris
 * ddi_strtol(9F) man page.  I have not verified the behavior of these
 * functions against their Solaris counterparts.  It is possible that I
 * may have misinterpreted the man page or the man page is incorrect.
 */
int ddi_strtol(const char *, char **, int, long *);
int ddi_strtoull(const char *, char **, int, unsigned long long *);
int ddi_strtoll(const char *, char **, int, long long *);

#define	define_ddi_strtox(type, valtype)				\
int ddi_strto##type(const char *str, char **endptr,			\
    int base, valtype *result)						\
{									\
	valtype last_value, value = 0;					\
	char *ptr = (char *)str;					\
	int digit, minus = 0;						\
									\
	while (strchr(" \t\n\r\f", *ptr))				\
		++ptr;							\
									\
	if (strlen(ptr) == 0)						\
		return (EINVAL);					\
									\
	switch (*ptr) {							\
	case '-':							\
		minus = 1;						\
		zfs_fallthrough;					\
	case '+':							\
		++ptr;							\
		break;							\
	}								\
									\
	/* Auto-detect base based on prefix */				\
	if (!base) {							\
		if (str[0] == '0') {					\
			if (tolower(str[1]) == 'x' && isxdigit(str[2])) { \
				base = 16; /* hex */			\
				ptr += 2;				\
			} else if (str[1] >= '0' && str[1] < 8) {	\
				base = 8; /* octal */			\
				ptr += 1;				\
			} else {					\
				return (EINVAL);			\
			}						\
		} else {						\
			base = 10; /* decimal */			\
		}							\
	}								\
									\
	while (1) {							\
		if (isdigit(*ptr))					\
			digit = *ptr - '0';				\
		else if (isalpha(*ptr))					\
			digit = tolower(*ptr) - 'a' + 10;		\
		else							\
			break;						\
									\
		if (digit >= base)					\
			break;						\
									\
		last_value = value;					\
		value = value * base + digit;				\
		if (last_value > value) /* Overflow */			\
			return (ERANGE);				\
									\
		ptr++;							\
	}								\
									\
	*result = minus ? -value : value;				\
									\
	if (endptr)							\
		*endptr = ptr;						\
									\
	return (0);							\
}									\

define_ddi_strtox(l, long)
define_ddi_strtox(ull, unsigned long long)
define_ddi_strtox(ll, long long)

EXPORT_SYMBOL(ddi_strtol);
EXPORT_SYMBOL(ddi_strtoll);
EXPORT_SYMBOL(ddi_strtoull);

int
ddi_copyin(const void *from, void *to, size_t len, int flags)
{
	/* Fake ioctl() issued by kernel, 'from' is a kernel address */
	if (flags & FKIOCTL) {
		memcpy(to, from, len);
		return (0);
	}

	return (copyin(from, to, len));
}
EXPORT_SYMBOL(ddi_copyin);

#define	define_spl_param(type, fmt)					\
int									\
spl_param_get_##type(char *buf, zfs_kernel_param_t *kp)			\
{									\
	return (scnprintf(buf, PAGE_SIZE, fmt "\n",			\
	    *(type *)kp->arg));						\
}									\
int									\
spl_param_set_##type(const char *buf, zfs_kernel_param_t *kp)		\
{									\
	return (kstrto##type(buf, 0, (type *)kp->arg));			\
}									\
const struct kernel_param_ops spl_param_ops_##type = {			\
	.set = spl_param_set_##type,					\
	.get = spl_param_get_##type,					\
};									\
EXPORT_SYMBOL(spl_param_get_##type);					\
EXPORT_SYMBOL(spl_param_set_##type);					\
EXPORT_SYMBOL(spl_param_ops_##type);

define_spl_param(s64, "%lld")
define_spl_param(u64, "%llu")

/*
 * Post a uevent to userspace whenever a new vdev adds to the pool. It is
 * necessary to sync blkid information with udev, which zed daemon uses
 * during device hotplug to identify the vdev.
 */
void
spl_signal_kobj_evt(struct block_device *bdev)
{
#if defined(HAVE_BDEV_KOBJ) || defined(HAVE_PART_TO_DEV)
#ifdef HAVE_BDEV_KOBJ
	struct kobject *disk_kobj = bdev_kobj(bdev);
#else
	struct kobject *disk_kobj = &part_to_dev(bdev->bd_part)->kobj;
#endif
	if (disk_kobj) {
		int ret = kobject_uevent(disk_kobj, KOBJ_CHANGE);
		if (ret) {
			pr_warn("ZFS: Sending event '%d' to kobject: '%s'"
			    " (%p): failed(ret:%d)\n", KOBJ_CHANGE,
			    kobject_name(disk_kobj), disk_kobj, ret);
		}
	}
#else
/*
 * This is encountered if neither bdev_kobj() nor part_to_dev() is available
 * in the kernel - likely due to an API change that needs to be chased down.
 */
#error "Unsupported kernel: unable to get struct kobj from bdev"
#endif
}
EXPORT_SYMBOL(spl_signal_kobj_evt);

int
ddi_copyout(const void *from, void *to, size_t len, int flags)
{
	/* Fake ioctl() issued by kernel, 'from' is a kernel address */
	if (flags & FKIOCTL) {
		memcpy(to, from, len);
		return (0);
	}

	return (copyout(from, to, len));
}
EXPORT_SYMBOL(ddi_copyout);

static ssize_t
spl_kernel_read(struct file *file, void *buf, size_t count, loff_t *pos)
{
#if defined(HAVE_KERNEL_READ_PPOS)
	return (kernel_read(file, buf, count, pos));
#else
	mm_segment_t saved_fs;
	ssize_t ret;

	saved_fs = get_fs();
	set_fs(KERNEL_DS);

	ret = vfs_read(file, (void __user *)buf, count, pos);

	set_fs(saved_fs);

	return (ret);
#endif
}

static int
spl_getattr(struct file *filp, struct kstat *stat)
{
	int rc;

	ASSERT(filp);
	ASSERT(stat);

#if defined(HAVE_4ARGS_VFS_GETATTR)
	rc = vfs_getattr(&filp->f_path, stat, STATX_BASIC_STATS,
	    AT_STATX_SYNC_AS_STAT);
#elif defined(HAVE_2ARGS_VFS_GETATTR)
	rc = vfs_getattr(&filp->f_path, stat);
#elif defined(HAVE_3ARGS_VFS_GETATTR)
	rc = vfs_getattr(filp->f_path.mnt, filp->f_dentry, stat);
#else
#error "No available vfs_getattr()"
#endif
	if (rc)
		return (-rc);

	return (0);
}

/*
 * Read the unique system identifier from the /etc/hostid file.
 *
 * The behavior of /usr/bin/hostid on Linux systems with the
 * regular eglibc and coreutils is:
 *
 *   1. Generate the value if the /etc/hostid file does not exist
 *      or if the /etc/hostid file is less than four bytes in size.
 *
 *   2. If the /etc/hostid file is at least 4 bytes, then return
 *      the first four bytes [0..3] in native endian order.
 *
 *   3. Always ignore bytes [4..] if they exist in the file.
 *
 * Only the first four bytes are significant, even on systems that
 * have a 64-bit word size.
 *
 * See:
 *
 *   eglibc: sysdeps/unix/sysv/linux/gethostid.c
 *   coreutils: src/hostid.c
 *
 * Notes:
 *
 * The /etc/hostid file on Solaris is a text file that often reads:
 *
 *   # DO NOT EDIT
 *   "0123456789"
 *
 * Directly copying this file to Linux results in a constant
 * hostid of 4f442023 because the default comment constitutes
 * the first four bytes of the file.
 *
 */

static char *spl_hostid_path = HW_HOSTID_PATH;
module_param(spl_hostid_path, charp, 0444);
MODULE_PARM_DESC(spl_hostid_path, "The system hostid file (/etc/hostid)");

static int
hostid_read(uint32_t *hostid)
{
	uint64_t size;
	uint32_t value = 0;
	int error;
	loff_t off;
	struct file *filp;
	struct kstat stat;

	filp = filp_open(spl_hostid_path, 0, 0);

	if (IS_ERR(filp))
		return (ENOENT);

	error = spl_getattr(filp, &stat);
	if (error) {
		filp_close(filp, 0);
		return (error);
	}
	size = stat.size;
	// cppcheck-suppress sizeofwithnumericparameter
	if (size < sizeof (HW_HOSTID_MASK)) {
		filp_close(filp, 0);
		return (EINVAL);
	}

	off = 0;
	/*
	 * Read directly into the variable like eglibc does.
	 * Short reads are okay; native behavior is preserved.
	 */
	error = spl_kernel_read(filp, &value, sizeof (value), &off);
	if (error < 0) {
		filp_close(filp, 0);
		return (EIO);
	}

	/* Mask down to 32 bits like coreutils does. */
	*hostid = (value & HW_HOSTID_MASK);
	filp_close(filp, 0);

	return (0);
}

/*
 * Return the system hostid.  Preferentially use the spl_hostid module option
 * when set, otherwise use the value in the /etc/hostid file.
 */
uint32_t
zone_get_hostid(void *zone)
{
	uint32_t hostid;

	ASSERT3P(zone, ==, NULL);

	if (spl_hostid != 0)
		return ((uint32_t)(spl_hostid & HW_HOSTID_MASK));

	if (hostid_read(&hostid) == 0)
		return (hostid);

	return (0);
}
EXPORT_SYMBOL(zone_get_hostid);

static int
spl_kvmem_init(void)
{
	int rc = 0;

	rc = spl_kmem_init();
	if (rc)
		return (rc);

	rc = spl_vmem_init();
	if (rc) {
		spl_kmem_fini();
		return (rc);
	}

	return (rc);
}

/*
 * We initialize the random number generator with 128 bits of entropy from the
 * system random number generator. In the improbable case that we have a zero
 * seed, we fallback to the system jiffies, unless it is also zero, in which
 * situation we use a preprogrammed seed. We step forward by 2^64 iterations to
 * initialize each of the per-cpu seeds so that the sequences generated on each
 * CPU are guaranteed to never overlap in practice.
 */
static int __init
spl_random_init(void)
{
	uint64_t s[4];
	int i = 0;

	spl_pseudo_entropy = __alloc_percpu(4 * sizeof (uint64_t),
	    sizeof (uint64_t));

	if (!spl_pseudo_entropy)
		return (-ENOMEM);

	get_random_bytes(s, sizeof (s));

	if (s[0] == 0 && s[1] == 0 && s[2] == 0 && s[3] == 0) {
		if (jiffies != 0) {
			s[0] = jiffies;
			s[1] = ~0 - jiffies;
			s[2] = ~jiffies;
			s[3] = jiffies - ~0;
		} else {
			(void) memcpy(s, "improbable seed", 16);
		}
		printk("SPL: get_random_bytes() returned 0 "
		    "when generating random seed. Setting initial seed to "
		    "0x%016llx%016llx%016llx%016llx.\n", cpu_to_be64(s[0]),
		    cpu_to_be64(s[1]), cpu_to_be64(s[2]), cpu_to_be64(s[3]));
	}

	for_each_possible_cpu(i) {
		uint64_t *wordp = per_cpu_ptr(spl_pseudo_entropy, i);

		spl_rand_jump(s);

		wordp[0] = s[0];
		wordp[1] = s[1];
		wordp[2] = s[2];
		wordp[3] = s[3];
	}

	return (0);
}

static void
spl_random_fini(void)
{
	free_percpu(spl_pseudo_entropy);
}

static void
spl_kvmem_fini(void)
{
	spl_vmem_fini();
	spl_kmem_fini();
}

static int __init
spl_init(void)
{
	int rc = 0;

	if ((rc = spl_random_init()))
		goto out0;

	if ((rc = spl_kvmem_init()))
		goto out1;

	if ((rc = spl_tsd_init()))
		goto out2;

	if ((rc = spl_taskq_init()))
		goto out3;

	if ((rc = spl_kmem_cache_init()))
		goto out4;

	if ((rc = spl_proc_init()))
		goto out5;

	if ((rc = spl_kstat_init()))
		goto out6;

	if ((rc = spl_zlib_init()))
		goto out7;

	if ((rc = spl_zone_init()))
		goto out8;

	return (rc);

out8:
	spl_zlib_fini();
out7:
	spl_kstat_fini();
out6:
	spl_proc_fini();
out5:
	spl_kmem_cache_fini();
out4:
	spl_taskq_fini();
out3:
	spl_tsd_fini();
out2:
	spl_kvmem_fini();
out1:
	spl_random_fini();
out0:
	return (rc);
}

static void __exit
spl_fini(void)
{
	spl_zone_fini();
	spl_zlib_fini();
	spl_kstat_fini();
	spl_proc_fini();
	spl_kmem_cache_fini();
	spl_taskq_fini();
	spl_tsd_fini();
	spl_kvmem_fini();
	spl_random_fini();
}

module_init(spl_init);
module_exit(spl_fini);

MODULE_DESCRIPTION("Solaris Porting Layer");
MODULE_AUTHOR(ZFS_META_AUTHOR);
MODULE_LICENSE("GPL");
MODULE_VERSION(ZFS_META_VERSION "-" ZFS_META_RELEASE);
