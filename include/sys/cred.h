#ifndef _SPL_CRED_H
#define _SPL_CRED_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <linux/module.h>
#include <sys/types.h>
#include <sys/vfs.h>

/* XXX - Portions commented out because we really just want to have the type
 * defined and the contents aren't nearly so important at the moment. */
typedef struct cred {
        uint_t          cr_ref;         /* reference count */
        uid_t           cr_uid;         /* effective user id */
        gid_t           cr_gid;         /* effective group id */
        uid_t           cr_ruid;        /* real user id */
        gid_t           cr_rgid;        /* real group id */
        uid_t           cr_suid;        /* "saved" user id (from exec) */
        gid_t           cr_sgid;        /* "saved" group id (from exec) */
        uint_t          cr_ngroups;     /* number of groups returned by */
                                        /* crgroups() */
#if 0
        cred_priv_t     cr_priv;        /* privileges */
        projid_t        cr_projid;      /* project */
        struct zone     *cr_zone;       /* pointer to per-zone structure */
        struct ts_label_s *cr_label;    /* pointer to the effective label */
        credsid_t       *cr_ksid;       /* pointer to SIDs */
#endif
        gid_t           cr_groups[1];   /* cr_groups size not fixed */
                                        /* audit info is defined dynamically */
                                        /* and valid only when audit enabled */
        /* auditinfo_addr_t     cr_auinfo;      audit info */
} cred_t;

#define kcred				NULL
#define CRED()				NULL

static __inline__ uid_t
crgetuid(cred_t *cr)
{
	return 0;
}

static __inline__ gid_t
crgetgid(cred_t *cr)
{
	return 0;
}

static __inline__ int
crgetngroups(cred_t *cr)
{
	return 0;
}

static __inline__ gid_t *
crgetgroups(cred_t *cr)
{
	return NULL;
}

static __inline__ int
groupmember(gid_t gid, const cred_t *cr)
{
	/* Primary group check */
	if ((cr) && (gid == cr->cr_gid))
		return 1;

	/* Supplemental group check (unsupported) */
	return 0;
}


#ifdef  __cplusplus
}
#endif

#endif  /* _SPL_CRED_H */
