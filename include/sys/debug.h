#ifndef _SPL_DEBUG_H
#define _SPL_DEBUG_H

extern unsigned long spl_debug_mask;
extern unsigned long spl_debug_subsys;

void __dprintf(const char *file, const char *func, int line, const char *fmt, ...);
void spl_set_debug_mask(unsigned long mask);
void spl_set_debug_subsys(unsigned long mask);

#endif /* SPL_DEBUG_H */
