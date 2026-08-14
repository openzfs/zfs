// SPDX-License-Identifier: CDDL-1.0
#ifndef _SYS_CRED_H
#define	_SYS_CRED_H

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef _KERNEL
#include <sys/param.h>
#ifndef NGROUPS
#ifdef NGROUPS_MAX
#define	NGROUPS	NGROUPS_MAX
#else
#define	NGROUPS	16
#endif
#endif
#define	__KAUTH_PRIVATE
#include <sys/kauth.h>

typedef struct kauth_cred	cred_t;

/*
 * CRED() is the calling LWP. kcred is a long-lived kernel cred taken
 * from proc0 at module load -- never alias the two. Callers that
 * crfree(kcred) must not drop the process cred.
 */
#define	CRED()			((cred_t *)kauth_cred_get())
extern cred_t *kcred;
void	spl_cred_init(void);
void	spl_cred_fini(void);

#define	KUID_TO_SUID(x)		(x)
#define	KGID_TO_SGID(x)		(x)
#define	crgetuid(cr)		kauth_cred_geteuid((kauth_cred_t)(cr))
#define	crgetruid(cr)		kauth_cred_getuid((kauth_cred_t)(cr))
#define	crgetgid(cr)		kauth_cred_getegid((kauth_cred_t)(cr))
#define	crgetrgid(cr)		kauth_cred_getgid((kauth_cred_t)(cr))
#define	crgetngroups(cr)	kauth_cred_ngroups((kauth_cred_t)(cr))
/*
 * Do not walk cr_groups -- that field is private kauth.  Index
 * with the public API.  dsl_deleg uses crgetgroupat().
 */
#define	crgetgroupat(cr, i) \
	kauth_cred_group((kauth_cred_t)(cr), (uint_t)(i))
#define	crgetzoneid(cr)		(0)
#define	crhold(cr)		kauth_cred_hold((kauth_cred_t)(cr))
#define	crfree(cr)		kauth_cred_free((kauth_cred_t)(cr))
#define	crgetsid(cr, i)		(NULL)

kauth_cred_t	zfs_lwp_cred_swap(cred_t *);
void		zfs_lwp_cred_restore(kauth_cred_t, cred_t *);

/*
 * Do not name this groupmember() -- NetBSD sys/proc.h already
 * prototypes that.  A static inline with the same name is
 * "static declaration follows non-static".
 */
static __inline int
zfs_cr_groupmember(gid_t gid, cred_t *cr)
{
	int result, error;

	error = kauth_cred_ismember_gid((kauth_cred_t)cr, gid, &result);
	if (error)
		return (0);
	return (kauth_cred_getegid((kauth_cred_t)cr) == gid || result);
}

#endif /* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_CRED_H */
