// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */
/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_SYSEVENT_DEV_H
#define	_SYS_SYSEVENT_DEV_H

#include <sys/sysevent/eventdefs.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Event schema for EC_DEV_ADD/ESC_DISK
 *
 *	Event Class 	- EC_DEV_ADD
 *	Event Sub-Class - ESC_DISK
 *
 *	Attribute Name	- EV_VERSION
 *	Attribute Type	- DATA_TYPE_INT32
 *	Attribute Value	- event version number
 *
 *	Attribute Name	- DEV_NAME
 *	Attribute Type	- DATA_TYPE_STRING
 *	Attribute Value	- /dev name to the raw device.
 *			  The name does not include the slice number component.
 *
 *	Attribute Name	- DEV_PHYS_PATH
 *	Attribute Type	- DATA_TYPE_STRING
 *	Attribute Value	- physical path of the device without the "/devices"
 *			  prefix.
 *
 *	Attribute Name	- DEV_DRIVER_NAME
 *	Attribute Type	- DATA_TYPE_STRING
 *	Attribute Value	- driver name
 *
 *	Attribute Name	- DEV_INSTANCE
 *	Attribute Type	- DATA_TYPE_INT32
 *	Attribute Value	- driver instance number
 *
 *	Attribute Name	- DEV_PROP_PREFIX<devinfo_node_property>
 *	Attribute Type	- data type of the devinfo_node_property
 *	Attribute Value	- value of the devinfo_node_property
 *
 *
 * Event schema for EC_DEV_ADD/ESC_NETWORK
 *
 *	Event Class 	- EC_DEV_ADD
 *	Event Sub-Class - ESC_NETWORK
 *
 *	Attribute Name	- EV_VERSION
 *	Attribute Type	- DATA_TYPE_INT32
 *	Attribute Value	- event version number
 *
 *	Attribute Name	- DEV_NAME
 *	Attribute Type	- DATA_TYPE_STRING
 *	Attribute Value	- /dev name associated with the device if exists.
 *			  /dev name associated with the driver for DLPI
 *			  Style-2 only drivers.
 *
 *	Attribute Name	- DEV_PHYS_PATH
 *	Attribute Type	- DATA_TYPE_STRING
 *	Attribute Value	- physical path of the device without the "/devices"
 *			  prefix.
 *
 *	Attribute Name	- DEV_DRIVER_NAME
 *	Attribute Type	- DATA_TYPE_STRING
 *	Attribute Value	- driver name
 *
 *	Attribute Name	- DEV_INSTANCE
 *	Attribute Type	- DATA_TYPE_INT32
 *	Attribute Value	- driver instance number
 *
 *	Attribute Name	- DEV_PROP_PREFIX<devinfo_node_property>
 *	Attribute Type	- data type of the devinfo_node_property
 *	Attribute Value	- value of the devinfo_node_property
 *
 *
 * Event schema for EC_DEV_ADD/ESC_PRINTER
 *
 *	Event Class 	- EC_DEV_ADD
 *	Event Sub-Class - ESC_PRINTER
 *
 *	Attribute Name	- EV_VERSION
 *	Attribute Type	- DATA_TYPE_INT32
 *	Attribute Value	- event version number
 *
 *	Attribute Name	- DEV_NAME
 *	Attribute Type	- DATA_TYPE_STRING
 *	Attribute Value	- /dev/printers name associated with the device
 *			  if exists.
 *			  /dev name associated with the device if it exists
 *
 *	Attribute Name	- DEV_PHYS_PATH
 *	Attribute Type	- DATA_TYPE_STRING
 *	Attribute Value	- physical path of the device without the "/devices"
 *			  prefix.
 *
 *	Attribute Name	- DEV_DRIVER_NAME
 *	Attribute Type	- DATA_TYPE_STRING
 *	Attribute Value	- driver name
 *
 *	Attribute Name	- DEV_INSTANCE
 *	Attribute Type	- DATA_TYPE_INT32
 *	Attribute Value	- driver instance number
 *
 *	Attribute Name	- DEV_PROP_PREFIX<devinfo_node_property>
 *	Attribute Type	- data type of the devinfo_node_property
 *	Attribute Value	- value of the devinfo_node_property
 *
 *
 * Event schema for EC_DEV_REMOVE/ESC_DISK
 *
 *	Event Class 	- EC_DEV_REMOVE
 *	Event Sub-Class - ESC_DISK
 *
 *	Attribute Name	- EV_VERSION
 *	Attribute Type	- DATA_TYPE_INT32
 *	Attribute Value	- event version number
 *
 *	Attribute Name	- DEV_NAME
 *	Attribute Type	- DATA_TYPE_STRING
 *	Attribute Value	- /dev name to the raw device.
 *			  The name does not include the slice number component.
 *
 *	Attribute Name	- DEV_PHYS_PATH
 *	Attribute Type	- DATA_TYPE_STRING
 *	Attribute Value	- physical path of the device without the "/devices"
 *			  prefix.
 *
 *	Attribute Name	- DEV_DRIVER_NAME
 *	Attribute Type	- DATA_TYPE_STRING
 *	Attribute Value	- driver name
 *
 *	Attribute Name	- DEV_INSTANCE
 *	Attribute Type	- DATA_TYPE_INT32
 *	Attribute Value	- driver instance number
 *
 *
 * Event schema for EC_DEV_REMOVE/ESC_NETWORK
 *
 *	Event Class 	- EC_DEV_REMOVE
 *	Event Sub-Class - ESC_NETWORK
 *
 *	Attribute Name	- EV_VERSION
 *	Attribute Type	- DATA_TYPE_INT32
 *	Attribute Value	- event version number
 *
 *	Attribute Name	- DEV_NAME
 *	Attribute Type	- DATA_TYPE_STRING
 *	Attribute Value	- /dev name associated with the device if exists.
 *			  /dev name associated with the driver for DLPI
 *			  Style-2 only drivers.
 *
 *	Attribute Name	- DEV_PHYS_PATH
 *	Attribute Type	- DATA_TYPE_STRING
 *	Attribute Value	- physical path of the device without the "/devices"
 *			  prefix.
 *
 *	Attribute Name	- DEV_DRIVER_NAME
 *	Attribute Type	- DATA_TYPE_STRING
 *	Attribute Value	- driver name
 *
 *	Attribute Name	- DEV_INSTANCE
 *	Attribute Type	- DATA_TYPE_INT32
 *	Attribute Value	- driver instance number
 *
 *
 * Event schema for EC_DEV_REMOVE/ESC_PRINTER
 *
 *	Event Class 	- EC_DEV_REMOVE
 *	Event Sub-Class - ESC_PRINTER
 *
 *	Attribute Name	- EV_VERSION
 *	Attribute Type	- DATA_TYPE_INT32
 *	Attribute Value	- event version number
 *
 *	Attribute Name	- DEV_NAME
 *	Attribute Type	- DATA_TYPE_STRING
 *	Attribute Value	- /dev/printers name associated with the device
 *			  if exists.
 *			  /dev name associated with the device if it exists
 *
 *	Attribute Name	- DEV_PHYS_PATH
 *	Attribute Type	- DATA_TYPE_STRING
 *	Attribute Value	- physical path of the device without the "/devices"
 *			  prefix.
 *
 *	Attribute Name	- DEV_DRIVER_NAME
 *	Attribute Type	- DATA_TYPE_STRING
 *	Attribute Value	- driver name
 *
 *	Attribute Name	- DEV_INSTANCE
 *	Attribute Type	- DATA_TYPE_INT32
 *	Attribute Value	- driver instance number
 *
 *
 * Event schema for EC_DEV_BRANCH/ESC_DEV_BRANCH_ADD or ESC_DEV_BRANCH_REMOVE
 *
 *	Event Class 	- EC_DEV_BRANCH
 *	Event Sub-Class - ESC_DEV_BRANCH_ADD or ESC_DEV_BRANCH_REMOVE
 *
 *	Attribute Name	- EV_VERSION
 *	Attribute Type	- DATA_TYPE_INT32
 *	Attribute Value	- event version number
 *
 *	Attribute Name	- DEV_PHYS_PATH
 *	Attribute Type	- DATA_TYPE_STRING
 *	Attribute Value	- physical path to the root node of the device subtree
 *			  without the "/devices" prefix.
 */

#define	EV_VERSION		"version"
#define	DEV_PHYS_PATH		"phys_path"
#define	DEV_NAME		"dev_name"
#define	DEV_DRIVER_NAME		"driver_name"
#define	DEV_INSTANCE		"instance"
#define	DEV_PROP_PREFIX		"prop-"

#ifdef __linux__
#define	DEV_IDENTIFIER		"devid"
#define	DEV_PATH		"path"
#define	DEV_IS_PART		"is_slice"
#define	DEV_SIZE		"dev_size"

/* Size of the whole parent block device (if dev is a partition) */
#define	DEV_PARENT_SIZE		"dev_parent_size"
#endif /* __linux__ */

#define	EV_V1			1

/* maximum number of devinfo node properties added to the event */
#define	MAX_PROP_COUNT		100

/* only properties with size less than PROP_LEN_LIMIT are added to the event */
#define	PROP_LEN_LIMIT		1024

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_SYSEVENT_DEV_H */
