// SPDX-License-Identifier: BSD-2-Clause
/*
 * Empty zfs.kmod -- satisfies module_autoload("zfs") from mount(2).
 *
 * OpenZFS is openzfs.kmod (VFS + SPL).  Loading this stub requires
 * openzfs, which vfs_attach()s the real zfs filesystem.
 */
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/module.h>
#include <sys/errno.h>

MODULE(MODULE_CLASS_MISC, zfs, "openzfs");

static int
zfs_modcmd(modcmd_t cmd, void *arg)
{
	(void) arg;
	switch (cmd) {
	case MODULE_CMD_INIT:
		return (0);
	case MODULE_CMD_FINI:
		return (0);
	default:
		return (ENOTTY);
	}
}
