
#ifndef _SPL_SYSTEMINFO_H
#define _SPL_SYSTEMINFO_H

#define HW_INVALID_HOSTID	0xFFFFFFFF	/* an invalid hostid */
#define HW_HOSTID_LEN		11		/* minimum buffer size needed */
						/* to hold a decimal or hex */
						/* hostid string */

const char *spl_panicstr(void);
int spl_system_inshutdown(void);


#endif /* SPL_SYSTEMINFO_H */
