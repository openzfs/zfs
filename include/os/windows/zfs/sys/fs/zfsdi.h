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
 * Copyright (c) 2020, DataCore Software Corp.
 */

#ifndef	_SYS_ZFSZVOLDI_H
#define	_SYS_ZFSZVOLDI_H

#include <guiddef.h>
#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The structures in this file are passed between kernel drivers who need to use
 * the zvol Direct Interface (DI).
 *
 * To resolve the interface use an IRP_MJ_PNP irp, initialize the stack the
 * following way and send it to the ZFS_DEV_KERNEL device object (use
 * IoGetDeviceObjectPointer() to resolve it)
 *
 *  pStack->MinorFunction = IRP_MN_QUERY_INTERFACE;
 *  pStack->Parameters.QueryInterface.InterfaceType = (LPGUID)&ZFSZVOLDI_GUID;
 *  pStack->Parameters.QueryInterface.Size = sizeof(zfsdizvol_t);
 *  pStack->Parameters.QueryInterface.Version = ZFSZVOLDI_VERSION;
 *  pStack->Parameters.QueryInterface.Interface = (PINTERFACE)<pointer on
 *	a zfsdizvol_t block>;
 *  pStack->Parameters.QueryInterface.InterfaceSpecificData = <pointer on a
 *	NULL-terminated string containing the ascii T10 value for the zvol>;
 *
 *	The full T10 value string is of the form <8-bytes-vendorid +
 *	    vendorspecific>: "OpenZFS poolname/zvolname"
 *
 * Upon a STATUS_SUCCESS return value the Context field in the INTERFACE
 * header portion of the zfsdizvol_t structure will be initialized. That Context
 * value is opaque, identifies the ZVOL being accessed, and must be set in all
 * interface calls.
 */

#define	ZFS_DEV_KERNEL		L"\\Device\\ZFSCTL"
DEFINE_GUID(ZFSZVOLDI_GUID, 0x904ca0cdl, 0x6ae1, 0x4acb, 0xb8, 0xb9, 0x2a, 0x00,
    0x2e, 0xd1, 0x10, 0xd4);

#define	ZFSZVOLDI_VERSION	1   // Interface version

/*
 * I/O Flags definition
 *
 * ZFSZVOLFG_AlwaysPend:
 *  When set, forces ZFS to always complete the I/O asynchronously through its
 *  own threading model. The caller will always get STATUS_PENDING back from
 *  the interface request.
 *  When not set, ZFS will use the caller's thread context to perform the direct
 *  interface I/O (if at all possible).
 */
#define	ZFSZVOLFG_AlwaysPend	0x1

/*
 * I/O descriptor
 *
 *  Each I/O call through the interface requires a zfsiodesc_t control block.
 *  The first 4 fields must be initialized, others are for use by the caller if
 *  needed and serve as optional callback routine and context values to that
 *  callback.
 *
 *  The ZFS driver will make its own copy of this control block if it can't
 *  handle the request synchronously so it can be allocated on the caller's
 *  stack (no need to perform a dynamic allocation for each I/O).
 *
 * Input values:
 *
 *  Buffer: must be in system address space. The caller is responsible for
 *	    getting that address mapped properly.
 *  ByteOffset: I/O offset, in bytes.
 *  Length: I/O length, in bytes
 *  Flags: 0 by default, see Flags definition above.
 *
 * Optional Caller input values:
 *
 *  Cb: Caller callback routine. NULL if none needed.
 *       Synchronous I/O return: the callback is always called prior to
 *		returning synchronously to the caller.
 *       Asynchronous I/O return: the callback is always called and the caller
 *		would have gotten STATUS_PENDING back from the request.
 *
 *       - pIo: could be different than the one passed in the request. However
 *		all fields will be copied from the request intact.
 *       - status: ultimate outcome for the I/O.
 *       - bPendingReturned: if TRUE, the caller has gotten STATUS_PENDING on
 *		its request. This can be used to set the upstream IRP pending.
 *              If FALSE, the request has been processed synchronously and the
 *		caller will get control just after the callback has run.
 *              ex: if the caller has logic to wait on an event in case of
 *		STATUS_PENDING, this flag can be used to skip setting the event
 *              since the request was not pended.
 *
 *   CbParm[]: 4 context values that the caller can use in its callback routine.
 *
 */
typedef struct zfsiodesc {
    PVOID Buffer;
    LONGLONG ByteOffset;
    ULONG Length;
    ULONG Flags; // Optional flags (see ZFSZVOLFG_xxxxxxx)
	// Optional I/O initiator's callback routine and parameter (must set
	// to NULL if not used)
	// Always called at final I/O completion by ZFS (whether the I/O is
	// synchronous or not).
    void(*Cb)(struct zfsiodesc *pIo, NTSTATUS status, BOOLEAN bPendingReturned);
    PVOID CbParm[4];
} zfsiodesc_t;

/*
 * Query interface descriptor.
 */
typedef struct {
	INTERFACE header;
    NTSTATUS(*Read)(PVOID Context, zfsiodesc_t *pIo);
    NTSTATUS(*Write)(PVOID Context, zfsiodesc_t *pIo);
	// Add new interface routines here. do not modify the existing order.
} zfsdizvol_t;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZFSZVOLDI_H */
