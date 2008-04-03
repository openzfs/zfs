#ifndef _SPL_SUNLDI_H
#define _SPL_SUNLDI_H

#include <sys/types.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/hdreg.h>
#include <linux/bio.h>

#define SECTOR_SIZE 512

typedef struct modlinkage {
	int ml_rev;
	struct modlfs *ml_modlfs;
	struct modldrv *ml_modldrv;
	major_t ml_major;
	unsigned ml_minors;
	void *pad1;
} modlinkage_t;

typedef struct ldi_ident {
	char li_modname[MAXNAMELEN];
	dev_t li_dev;
} *ldi_ident_t;

typedef struct block_device *ldi_handle_t;

extern int ldi_ident_from_mod(struct modlinkage *modlp, ldi_ident_t *lip);
extern void ldi_ident_release(ldi_ident_t li);

#endif /* SPL_SUNLDI_H */
