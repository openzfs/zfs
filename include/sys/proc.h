#ifndef _SPL_PROC_H
#define _SPL_PROC_H

#include <linux/proc_fs.h>
#include <linux/kmod.h>
#include <linux/uaccess.h>
#include <linux/ctype.h>
#include <linux/sysctl.h>
#include <linux/seq_file.h>
#include <sys/sysmacros.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/kstat.h>
#include <sys/debug.h>

#ifdef DEBUG_KSTAT
extern struct proc_dir_entry *proc_sys_spl_kstat;
struct proc_dir_entry *proc_dir_entry_find(struct proc_dir_entry *root,
					   const char *str);
int proc_dir_entries(struct proc_dir_entry *root);
#endif

int proc_init(void);
void proc_fini(void);

#endif /* SPL_PROC_H */
