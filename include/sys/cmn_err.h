#ifndef _SPL_CMN_ERR_H
#define _SPL_CMN_ERR_H

#include <sys/varargs.h>

#define CE_CONT         0       /* continuation         */
#define CE_NOTE         1       /* notice               */
#define CE_WARN         2       /* warning              */
#define CE_PANIC        3       /* panic                */
#define CE_IGNORE       4       /* print nothing        */

extern void cmn_err(int, const char *, ...);
extern void vcmn_err(int, const char *, __va_list);
extern void vpanic(const char *, __va_list);

#endif /* SPL_CMN_ERR_H */
