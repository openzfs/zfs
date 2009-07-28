#ifndef _SPL_CRED_H
#define _SPL_CRED_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <linux/module.h>
#include <sys/types.h>
#include <sys/vfs.h>

#ifdef HAVE_CRED_STRUCT

typedef struct cred cred_t;

#define kcred		((cred_t *)(init_task.cred))
#define CRED()		((cred_t *)current_cred())

#else

typedef struct task_struct cred_t;

#define kcred		((cred_t *)&init_task)
#define CRED()		((cred_t *)current)

#endif /* HAVE_CRED_STRUCT */

extern void crhold(cred_t *cr);
extern void crfree(cred_t *cr);
extern uid_t crgetuid(const cred_t *cr);
extern uid_t crgetruid(const cred_t *cr);
extern uid_t crgetsuid(const cred_t *cr);
extern gid_t crgetgid(const cred_t *cr);
extern gid_t crgetrgid(const cred_t *cr);
extern gid_t crgetsgid(const cred_t *cr);
extern int crgetngroups(const cred_t *cr);
extern gid_t * crgetgroups(const cred_t *cr);
extern int groupmember(gid_t gid, const cred_t *cr);

#ifdef  __cplusplus
}
#endif

#endif  /* _SPL_CRED_H */
