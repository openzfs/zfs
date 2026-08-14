// SPDX-License-Identifier: BSD-2-Clause
/*
 * NetBSD has no jails.  Zone/dataset visibility is always global.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/zone.h>
#include <sys/cred.h>
#include <sys/fcntl.h>

int
zone_dataset_attach(cred_t *cred, const char *dataset, int jailid)
{
	(void) cred;
	(void) dataset;
	(void) jailid;
	return (0);
}

int
zone_dataset_detach(cred_t *cred, const char *dataset, int jailid)
{
	(void) cred;
	(void) dataset;
	(void) jailid;
	return (0);
}

int
zone_dataset_visible(const char *dataset, int *writeok)
{
	(void) dataset;
	if (writeok != NULL)
		*writeok = 1;
	return (1);
}

uint32_t
zone_get_hostid(void *zone)
{
	(void) zone;
	/* Stable system hostid -- do not invent a random value per call. */
	return ((uint32_t)hostid);
}
