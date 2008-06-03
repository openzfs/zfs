#ifndef _SPL_UTSNAME_H
#define _SPL_UTSNAME_H

#include <linux/utsname.h>

extern struct new_utsname *__utsname(void);

#define utsname			(*__utsname())

#endif /* SPL_UTSNAME_H */
