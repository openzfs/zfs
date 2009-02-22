#ifndef _SPL_PARAM_H
#define _SPL_PARAM_H

/* Pages to bytes and back */
#define ptob(pages)			(pages * PAGE_SIZE)
#define btop(bytes)			(bytes / PAGE_SIZE)

#endif /* SPL_PARAM_H */
