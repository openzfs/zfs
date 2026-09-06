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
 * Copyright (c) 2013-2020, Jorgen Lundman.  All rights reserved.
 */

#include <sys/types.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOBSD.h>
#include <IOKit/IOKitKeys.h>

#include <sys/zfs_vfsops.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_znode.h>
#include <sys/dataset_kstats.h>
#include <sys/zvol.h>

#include <sys/zvolIO.h>
#include <sys/ldi_osx.h>

#include <sys/zfs_vnops.h>
#include <sys/taskq.h>
#include <sys/spa_impl.h>
#include <sys/zfs_boot.h>

#include <libkern/version.h>
#include <libkern/sysctl.h>

#include <zfs_gitrev.h>

// Define the superclass.
#define	super IOService

OSDefineMetaClassAndStructors(org_openzfsonosx_zfs_zvol, IOService)

extern "C" {

#include <sys/zfs_ioctl_impl.h>
#include <sys/utsname.h>
#include <string.h>

extern SInt32 zfs_active_fs_count;

#ifdef DEBUG
#define	ZFS_DEBUG_STR	" (DEBUG mode)"
#else
#define	ZFS_DEBUG_STR	""
#endif

static char spl_gitrev[64] = ZFS_META_VERSION "-" ZFS_META_RELEASE;

SYSCTL_DECL(_zfs);
SYSCTL_NODE(, OID_AUTO, zfs, CTLFLAG_RD, 0, "");
SYSCTL_STRING(_zfs, OID_AUTO, kext_version,
    CTLFLAG_RD | CTLFLAG_LOCKED,
    spl_gitrev, 0, "ZFS KEXT Version");


extern kern_return_t _start(kmod_info_t *ki, void *data);
extern kern_return_t _stop(kmod_info_t *ki, void *data);

__attribute__((visibility("default"))) KMOD_EXPLICIT_DECL(org.openzfsonosx.zfs,
    "1.0.0", _start, _stop)
kmod_start_func_t *_realmain = 0;
kmod_stop_func_t  *_antimain = 0;
int _kext_apple_cc = __APPLE_CC__;

} // Extern "C"

bool
org_openzfsonosx_zfs_zvol::init(OSDictionary* dict)
{
	bool res;

	/* Need an OSSet for open clients */
	_openClients = OSSet::withCapacity(1);
	if (_openClients == NULL) {
		dprintf("client OSSet failed");
		return (false);
	}

	res = super::init(dict);

	// IOLog("ZFS::init\n");
	return (res);
}

void
org_openzfsonosx_zfs_zvol::free(void)
{
	OSSafeReleaseNULL(_openClients);

	// IOLog("ZFS::free\n");
	super::free();
}

bool
org_openzfsonosx_zfs_zvol::isOpen(const IOService *forClient) const
{
	bool ret;
	ret = IOService::isOpen(forClient);
	return (ret);
}

bool
org_openzfsonosx_zfs_zvol::handleOpen(IOService *client,
    IOOptionBits options, void *arg)
{
	bool ret = true;

	dprintf("");

	_openClients->setObject(client);
	ret = _openClients->containsObject(client);

	return (ret);
}

bool
org_openzfsonosx_zfs_zvol::handleIsOpen(const IOService *client) const
{
	bool ret;

	dprintf("");

	ret = _openClients->containsObject(client);

	return (ret);
}

void
org_openzfsonosx_zfs_zvol::handleClose(IOService *client,
    IOOptionBits options)
{
	dprintf("");

	if (_openClients->containsObject(client) == false) {
		dprintf("not open");
	}

	_openClients->removeObject(client);
}

IOService*
org_openzfsonosx_zfs_zvol::probe(IOService *provider, SInt32 *score)
{
	IOService *res = super::probe(provider, score);
	return (res);
}


/*
 *
 * ************************************************************************
 *
 * Kernel Module Load
 *
 * ************************************************************************
 *
 */

bool
org_openzfsonosx_zfs_zvol::start(IOService *provider)
{
	bool res = super::start(provider);

	IOLog("ZFS: Loading module ... \n");

	if (!res)
		return (res);

	/* Fire up all SPL modules and threads */
	spl_start(NULL, NULL);

	/* registerService() allows zconfigd to match against the service */
	this->registerService();

	/*
	 * hostid is left as 0 on OSX, and left to be set if developers wish to
	 * use it. If it is 0, we will hash the hardware.uuid into a 32 bit
	 * value and set the hostid.
	 */
	if (!zone_get_hostid(NULL)) {
		uint32_t myhostid = 0;
		IORegistryEntry *ioregroot =
		    IORegistryEntry::getRegistryRoot();
		if (ioregroot) {
			IORegistryEntry *macmodel =
			    ioregroot->getChildEntry(gIOServicePlane);

			if (macmodel) {
				OSObject *ioplatformuuidobj;
				ioplatformuuidobj =
				    macmodel->getProperty(kIOPlatformUUIDKey);
				if (ioplatformuuidobj) {
					OSString *ioplatformuuidstr =
					    OSDynamicCast(OSString,
					    ioplatformuuidobj);

					myhostid = fnv_32a_str(
					    ioplatformuuidstr->
					    getCStringNoCopy(),
					    FNV1_32A_INIT);

					sysctlbyname("kern.hostid", NULL, NULL,
					    &myhostid, sizeof (myhostid));
					printf("ZFS: hostid set to %08x from "
					    "UUID '%s'\n", myhostid,
					    ioplatformuuidstr->
					    getCStringNoCopy());
				}
			}
		}
	}

	/* Register ZFS KEXT Version sysctl - separate to kstats */
	sysctl_register_oid(&sysctl__zfs);
	sysctl_register_oid(&sysctl__zfs_kext_version);

	/* Init LDI */
	int error = 0;
	error = ldi_init(NULL);
	if (error) {
		IOLog("%s ldi_init error %d\n", __func__, error);
		goto failure;
	}

	/* Start ZFS itself */
	zfs_kmod_init();

	/* Register fs with XNU */
	zfs_vfsops_init();

	/*
	 * When is the best time to start the system_taskq? It is strictly
	 * speaking not used by SPL, but by ZFS. ZFS should really start it?
	 */
	system_taskq_init();

	res = zfs_boot_init((IOService *)this);

	printf("ZFS: Loaded module v%s-%s%s, "
	    "ZFS pool version %s, ZFS filesystem version %s\n",
	    ZFS_META_VERSION, ZFS_META_RELEASE, ZFS_DEBUG_STR,
	    SPA_VERSION_STRING, ZPL_VERSION_STRING);

	return (true);

failure:
	spl_stop(NULL, NULL);
	sysctl_unregister_oid(&sysctl__zfs_kext_version);
	sysctl_unregister_oid(&sysctl__zfs);
	return (false);
}

/* Here we are, at the end of all things */
void
org_openzfsonosx_zfs_zvol::stop(IOService *provider)
{

	zfs_boot_fini();

	IOLog("ZFS: Attempting to unload ...\n");

	super::stop(provider);

	zfs_vfsops_fini();

	zfs_kmod_fini();

	system_taskq_fini();

	ldi_fini();

	sysctl_unregister_oid(&sysctl__zfs_kext_version);
	sysctl_unregister_oid(&sysctl__zfs);

	spl_stop(NULL, NULL);

	printf("ZFS: Unloaded module v%s-%s%s\n",
	    ZFS_META_VERSION, ZFS_META_RELEASE, ZFS_DEBUG_STR);

	/*
	 * There is no way to ensure all threads have actually got to the
	 * thread_exit() call, before we exit here (and XNU unloads all
	 * memory for the KEXT). So we increase the odds of that happening
	 * by delaying a little bit before we return to XNU. Quite possibly
	 * the worst "solution" but Apple has not given any good options.
	 */
	delay(hz*5);
}
