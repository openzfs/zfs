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

#include <sys/zio.h>
#include <sys/vdev_file.h>

/*
 * FreeBSD has no need to lazily reopen file vdevs before I/O; this
 * only exists so that vdev_file_io_start() can call it unconditionally
 * across all platforms. See the macOS implementation in
 * module/os/macos/zfs/vdev_file_os.c for what this hook is for.
 */
int
vdev_file_os_io_start(zio_t *zio)
{
	(void) zio;
	return (0);
}
