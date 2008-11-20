/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include "zfs_config.h"

#ifdef HAVE_LIBDISKMGT_H
#include_next <libdiskmgt.h>
#else

#ifndef _LIBDISKMGT_H
#define	_LIBDISKMGT_H



#ifdef __cplusplus
extern "C" {
#endif

#include <libnvpair.h>
#include <sys/swap.h>


/*
 * Holds all the data regarding the device.
 * Private to libdiskmgt. Must use dm_xxx functions to set/get data.
 */
typedef uint64_t  dm_descriptor_t;

typedef enum {
	DM_WHO_MKFS = 0,
	DM_WHO_ZPOOL,
	DM_WHO_ZPOOL_FORCE,
	DM_WHO_FORMAT,
	DM_WHO_SWAP,
	DM_WHO_DUMP,
	DM_WHO_ZPOOL_SPARE
} dm_who_type_t;

typedef enum {
    DM_DRIVE = 0,
    DM_CONTROLLER,
    DM_MEDIA,
    DM_SLICE,
    DM_PARTITION,
    DM_PATH,
    DM_ALIAS,
    DM_BUS
} dm_desc_type_t;


typedef enum {
    DM_DT_UNKNOWN = 0,
    DM_DT_FIXED,
    DM_DT_ZIP,
    DM_DT_JAZ,
    DM_DT_FLOPPY,
    DM_DT_MO_ERASABLE,
    DM_DT_MO_WRITEONCE,
    DM_DT_AS_MO,
    DM_DT_CDROM,
    DM_DT_CDR,
    DM_DT_CDRW,
    DM_DT_DVDROM,
    DM_DT_DVDR,
    DM_DT_DVDRAM,
    DM_DT_DVDRW,
    DM_DT_DDCDROM,
    DM_DT_DDCDR,
    DM_DT_DDCDRW
} dm_drive_type_t;

typedef enum {
    DM_MT_UNKNOWN = 0,
    DM_MT_FIXED,
    DM_MT_FLOPPY,
    DM_MT_CDROM,
    DM_MT_ZIP,
    DM_MT_JAZ,
    DM_MT_CDR,
    DM_MT_CDRW,
    DM_MT_DVDROM,
    DM_MT_DVDR,
    DM_MT_DVDRAM,
    DM_MT_MO_ERASABLE,
    DM_MT_MO_WRITEONCE,
    DM_MT_AS_MO
} dm_media_type_t;

#define	DM_FILTER_END	-1

/* drive stat name */
typedef enum {
    DM_DRV_STAT_PERFORMANCE = 0,
    DM_DRV_STAT_DIAGNOSTIC,
    DM_DRV_STAT_TEMPERATURE
} dm_drive_stat_t;

/* slice stat name */
typedef enum {
    DM_SLICE_STAT_USE = 0
} dm_slice_stat_t;

/* attribute definitions */

/* drive */
#define	DM_DISK_UP		1
#define	DM_DISK_DOWN		0

#define	DM_CLUSTERED		"clustered"
#define	DM_DRVTYPE		"drvtype"
#define	DM_FAILING		"failing"
#define	DM_LOADED		"loaded"	/* also in media */
#define	DM_NDNRERRS		"ndevice_not_ready_errors"
#define	DM_NBYTESREAD		"nbytes_read"
#define	DM_NBYTESWRITTEN	"nbytes_written"
#define	DM_NHARDERRS		"nhard_errors"
#define	DM_NILLREQERRS		"nillegal_req_errors"
#define	DM_NMEDIAERRS		"nmedia_errors"
#define	DM_NNODEVERRS		"nno_dev_errors"
#define	DM_NREADOPS		"nread_ops"
#define	DM_NRECOVERRS		"nrecoverable_errors"
#define	DM_NSOFTERRS		"nsoft_errors"
#define	DM_NTRANSERRS		"ntransport_errors"
#define	DM_NWRITEOPS		"nwrite_ops"
#define	DM_OPATH		"opath"
#define	DM_PRODUCT_ID		"product_id"
#define	DM_REMOVABLE		"removable"	/* also in media */
#define	DM_RPM			"rpm"
#define	DM_STATUS		"status"
#define	DM_SYNC_SPEED		"sync_speed"
#define	DM_TEMPERATURE		"temperature"
#define	DM_VENDOR_ID		"vendor_id"
#define	DM_WIDE			"wide"		/* also on controller */
#define	DM_WWN			"wwn"

/* bus */
#define	DM_BTYPE		"btype"
#define	DM_CLOCK		"clock"		/* also on controller */
#define	DM_PNAME		"pname"

/* controller */
#define	DM_FAST			"fast"
#define	DM_FAST20		"fast20"
#define	DM_FAST40		"fast40"
#define	DM_FAST80		"fast80"
#define	DM_MULTIPLEX		"multiplex"
#define	DM_PATH_STATE		"path_state"

#define	DM_CTYPE_ATA		"ata"
#define	DM_CTYPE_SCSI		"scsi"
#define	DM_CTYPE_FIBRE		"fibre channel"
#define	DM_CTYPE_USB		"usb"
#define	DM_CTYPE_UNKNOWN	"unknown"

/* media */
#define	DM_BLOCKSIZE		"blocksize"
#define	DM_FDISK		"fdisk"
#define	DM_MTYPE		"mtype"
#define	DM_NACTUALCYLINDERS	"nactual_cylinders"
#define	DM_NALTCYLINDERS	"nalt_cylinders"
#define	DM_NCYLINDERS		"ncylinders"
#define	DM_NHEADS		"nheads"
#define	DM_NPHYSCYLINDERS	"nphys_cylinders"
#define	DM_NSECTORS		"nsectors"	/* also in partition */
#define	DM_SIZE			"size"		/* also in slice */
#define	DM_NACCESSIBLE		"naccessible"
#define	DM_LABEL		"label"

/* partition */
#define	DM_BCYL			"bcyl"
#define	DM_BHEAD		"bhead"
#define	DM_BOOTID		"bootid"
#define	DM_BSECT		"bsect"
#define	DM_ECYL			"ecyl"
#define	DM_EHEAD		"ehead"
#define	DM_ESECT		"esect"
#define	DM_PTYPE		"ptype"
#define	DM_RELSECT		"relsect"

/* slice */
#define	DM_DEVICEID		"deviceid"
#define	DM_DEVT			"devt"
#define	DM_INDEX		"index"
#define	DM_EFI_NAME		"name"
#define	DM_MOUNTPOINT		"mountpoint"
#define	DM_LOCALNAME		"localname"
#define	DM_START		"start"
#define	DM_TAG			"tag"
#define	DM_FLAG			"flag"
#define	DM_EFI			"efi"	/* also on media */
#define	DM_USED_BY		"used_by"
#define	DM_USED_NAME		"used_name"
#define	DM_USE_MOUNT		"mount"
#define	DM_USE_SVM		"svm"
#define	DM_USE_LU		"lu"
#define	DM_USE_DUMP		"dump"
#define	DM_USE_VXVM		"vxvm"
#define	DM_USE_FS		"fs"
#define	DM_USE_VFSTAB		"vfstab"
#define	DM_USE_EXPORTED_ZPOOL	"exported_zpool"
#define	DM_USE_ACTIVE_ZPOOL	"active_zpool"
#define	DM_USE_SPARE_ZPOOL	"spare_zpool"
#define	DM_USE_L2CACHE_ZPOOL	"l2cache_zpool"

/* event */
#define	DM_EV_NAME		"name"
#define	DM_EV_DTYPE		"edtype"
#define	DM_EV_TYPE		"evtype"
#define	DM_EV_TADD		"add"
#define	DM_EV_TREMOVE		"remove"
#define	DM_EV_TCHANGE		"change"

/* findisks */
#define	DM_CTYPE		"ctype"
#define	DM_LUN			"lun"
#define	DM_TARGET		"target"

#define	NOINUSE_SET	getenv("NOINUSE_CHECK") != NULL

void			dm_free_descriptors(dm_descriptor_t *desc_list);
void			dm_free_descriptor(dm_descriptor_t desc);
void			dm_free_name(char *name);
void			dm_free_swapentries(swaptbl_t *);

dm_descriptor_t		*dm_get_descriptors(dm_desc_type_t type, int filter[],
			    int *errp);
dm_descriptor_t		*dm_get_associated_descriptors(dm_descriptor_t desc,
			    dm_desc_type_t type, int *errp);
dm_desc_type_t		*dm_get_associated_types(dm_desc_type_t type);
dm_descriptor_t		dm_get_descriptor_by_name(dm_desc_type_t desc_type,
			    char *name, int *errp);
char			*dm_get_name(dm_descriptor_t desc, int *errp);
dm_desc_type_t		dm_get_type(dm_descriptor_t desc);
nvlist_t		*dm_get_attributes(dm_descriptor_t desc, int *errp);
nvlist_t		*dm_get_stats(dm_descriptor_t desc, int stat_type,
			    int *errp);
void			dm_init_event_queue(void(*callback)(nvlist_t *, int),
			    int *errp);
nvlist_t		*dm_get_event(int *errp);
void			dm_get_slices(char *drive, dm_descriptor_t **slices,
			    int *errp);
void			dm_get_slice_stats(char *slice, nvlist_t **dev_stats,
			    int *errp);
int			dm_get_swapentries(swaptbl_t **, int *);
void			dm_get_usage_string(char *who, char *data, char **msg);
int			dm_inuse(char *dev_name, char **msg, dm_who_type_t who,
			    int *errp);
int			dm_inuse_swap(const char *dev_name, int *errp);
int			dm_isoverlapping(char *dev_name, char **msg, int *errp);

#ifdef __cplusplus
}
#endif

#endif /* _LIBDISKMGT_H */
#endif /* HAVE_LIBDISKMGT_H */
