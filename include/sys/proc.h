#ifndef _SPL_PROC_H
#define _SPL_PROC_H

#include <linux/proc_fs.h>

int proc_init(void);
void proc_fini(void);

#endif /* SPL_PROC_H */
