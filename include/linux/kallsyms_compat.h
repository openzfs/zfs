#ifndef _SPL_KALLSYMS_COMPAT_H
#define _SPL_KALLSYMS_COMPAT_H

#ifdef HAVE_KALLSYMS_LOOKUP_NAME

#include <linux/kallsyms.h>
#define spl_kallsyms_lookup_name(name) kallsyms_lookup_name(name)

#else

typedef unsigned long (*kallsyms_lookup_name_t)(const char *);
extern kallsyms_lookup_name_t spl_kallsyms_lookup_name_fn;
#define spl_kallsyms_lookup_name(name) spl_kallsyms_lookup_name_fn(name)

#endif /* HAVE_KALLSYMS_LOOKUP_NAME */

#endif /* _SPL_KALLSYMS_COMPAT_H */
