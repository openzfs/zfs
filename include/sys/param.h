#ifndef _SPL_PARAM_H
#define _SPL_PARAM_H

#include <asm/page.h>

/* Pages to bytes and back */
#define ptob(pages)			(pages << PAGE_SHIFT)
#define btop(bytes)			(bytes >> PAGE_SHIFT)

#endif /* SPL_PARAM_H */
