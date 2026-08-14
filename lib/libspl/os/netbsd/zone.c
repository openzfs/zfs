// SPDX-License-Identifier: BSD-2-Clause
/*
 * NetBSD has no jails; always report the illumos global zone (0).
 * Never return the Linux root-userns inode (4026531837) -- a mismatch
 * against GLOBAL_ZONEID==0 makes every `zfs create` return EPERM.
 */
#include <stdlib.h>
#include <sys/types.h>
#include <zone.h>

zoneid_t
getzoneid(void)
{
	return (0);
}
