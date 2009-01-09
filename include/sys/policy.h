#ifndef _SPL_POLICY_H
#define _SPL_POLICY_H

#define secpolicy_fs_unmount(c,vfs)	(0)
#define secpolicy_nfs(c)		(0)
#define secpolicy_sys_config(c,co)	(0)
#define secpolicy_zfs(c)		(0)
#define secpolicy_zinject(c)		(0)

#endif /* SPL_POLICY_H */
