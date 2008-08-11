#ifndef _SPL_UACCESS_COMPAT_H
#define _SPL_UACCESS_COMPAT_H

#ifdef HAVE_UACCESS_HEADER
#include <linux/uaccess.h>
#else
#include <asm/uaccess.h>
#endif

#endif /* _SPL_UACCESS_COMPAT_H */

