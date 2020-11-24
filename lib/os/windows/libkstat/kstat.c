/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 * Copyright 2015 Nexenta Systems, Inc. All rights reserved.
 * Copyright 2018 Jorgen Lundman <lundman@lundman.net>. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h>
#include <memory.h>
#include <strings.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>

#include <sys/kstat.h>

#include "kstat.h"

/*LINTLIBRARY*/

static void
kstat_zalloc(void **ptr, size_t size, int free_first)
{
	if (free_first)
		free(*ptr);
	*ptr = calloc(size, 1);
}

static void
kstat_chain_free(kstat_ctl_t *kc)
{
	kstat_t *ksp, *nksp;

	ksp = kc->kc_chain;
	while (ksp) {
		nksp = ksp->ks_next;
		free(ksp->ks_data);
		free(ksp);
		ksp = nksp;
	}
	kc->kc_chain = NULL;
	kc->kc_chain_id = 0;
}

kstat_ctl_t *
kstat_open(void)
{
	kstat_ctl_t *kc;
	HANDLE h;

	h = CreateFile("\\\\.\\ZFS", GENERIC_READ | GENERIC_WRITE, // ZFSDEV - no includes
		0, NULL, OPEN_EXISTING, 0, NULL);
	if (h == INVALID_HANDLE_VALUE) 
		return (NULL);

	kstat_zalloc((void **)&kc, sizeof (kstat_ctl_t), 0);
	if (kc == NULL)
		return (NULL);

	kc->kc_kd = h;
	kc->kc_chain = NULL;
	kc->kc_chain_id = 0;
	if (kstat_chain_update(kc) == -1) {
		int saved_err = errno;
		(void) kstat_close(kc);
		errno = saved_err;
		return (NULL);
	}
	return (kc);
}

int
kstat_close(kstat_ctl_t *kc)
{
	int rc = 0;

	kstat_chain_free(kc);
	CloseHandle(kc->kc_kd);
	free(kc);
	return (rc);
}

int kstat_ioctl(HANDLE hDevice, int request, kstat_t *ksp)
{
	int error;
	ULONG bytesReturned;

	error = DeviceIoControl(hDevice,
		(DWORD)request,
		ksp,
		(DWORD)sizeof(kstat_t),
		ksp,
		(DWORD)sizeof(kstat_t),
		&bytesReturned,
		NULL
	);

	// Windows: error from DeviceIoControl() is unlikely
	if (error == 0) {
		error = GetLastError();
	} else {
		// More likely is we return error from kernel in errnovalue,
		// or value in returnvalue
		errno = ksp->ks_errnovalue;
		error = ksp->ks_returnvalue;
	}

	return error;
}

kid_t
kstat_read(kstat_ctl_t *kc, kstat_t *ksp, void *data)
{
	kid_t kcid;

	if (ksp->ks_data == NULL && ksp->ks_data_size > 0) {
		kstat_zalloc(&ksp->ks_data, ksp->ks_data_size, 0);
		if (ksp->ks_data == NULL)
			return (-1);
	}
	while ((kcid = (kid_t)kstat_ioctl(kc->kc_kd, KSTAT_IOC_READ, ksp)) == -1) {
			if (errno == EAGAIN) {
			(void) usleep(100);	/* back off a moment */
			continue;			/* and try again */
		}
		/*
		 * Mating dance for variable-size kstats.
		 * You start with a buffer of a certain size,
		 * which you hope will hold all the data.
		 * If your buffer is too small, the kstat driver
		 * returns ENOMEM and sets ksp->ks_data_size to
		 * the current size of the kstat's data.  You then
		 * resize your buffer and try again.  In practice,
		 * this almost always converges in two passes.
		 */
		if (errno == ENOMEM && (ksp->ks_flags &
		    (KSTAT_FLAG_VAR_SIZE | KSTAT_FLAG_LONGSTRINGS))) {
			kstat_zalloc(&ksp->ks_data, ksp->ks_data_size, 1);
			if (ksp->ks_data != NULL)
				continue;
		}
		return (-1);
	}
	if (data != NULL) {
		(void) memcpy(data, ksp->ks_data, ksp->ks_data_size);
		if (ksp->ks_type == KSTAT_TYPE_NAMED &&
		    ksp->ks_data_size !=
		    ksp->ks_ndata * sizeof (kstat_named_t)) {
			/*
			 * Has KSTAT_DATA_STRING fields. Fix the pointers.
			 */
			uint_t i;
			kstat_named_t *knp = data;

			for (i = 0; i < ksp->ks_ndata; i++, knp++) {
				if (knp->data_type != KSTAT_DATA_STRING)
					continue;
				if (KSTAT_NAMED_STR_PTR(knp) == NULL)
					continue;
				/*
				 * The offsets of the strings within the
				 * buffers are the same, so add the offset of
				 * the string to the beginning of 'data' to fix
				 * the pointer so that strings in 'data' don't
				 * point at memory in 'ksp->ks_data'.
				 */
				KSTAT_NAMED_STR_PTR(knp) = (char *)data +
				    (KSTAT_NAMED_STR_PTR(knp) -
				    (char *)ksp->ks_data);
			}
		}
	}
	return (kcid);
}

kid_t
kstat_write(kstat_ctl_t *kc, kstat_t *ksp, void *data)
{
	kid_t kcid;

	if (ksp->ks_data == NULL && ksp->ks_data_size > 0) {
		kstat_zalloc(&ksp->ks_data, ksp->ks_data_size, 0);
		if (ksp->ks_data == NULL)
			return (-1);
	}
	if (data != NULL) {
		(void) memcpy(ksp->ks_data, data, ksp->ks_data_size);
		if (ksp->ks_type == KSTAT_TYPE_NAMED) {
			kstat_named_t *oknp = data;
			kstat_named_t *nknp = KSTAT_NAMED_PTR(ksp);
			uint_t i;

			for (i = 0; i < ksp->ks_ndata; i++, oknp++, nknp++) {
				if (nknp->data_type != KSTAT_DATA_STRING)
					continue;
				if (KSTAT_NAMED_STR_PTR(nknp) == NULL)
					continue;
				/*
				 * The buffer passed in as 'data' has string
				 * pointers that point within 'data'.  Fix the
				 * pointers so they point into the same offset
				 * within the newly allocated buffer.
				 */
				KSTAT_NAMED_STR_PTR(nknp) =
				    (char *)ksp->ks_data +
				    (KSTAT_NAMED_STR_PTR(oknp) - (char *)data);
			}
		}

	}
	while ((kcid = (kid_t)kstat_ioctl(kc->kc_kd, KSTAT_IOC_WRITE, ksp)) == -1) {
		if (errno == EAGAIN) {
			(void) usleep(100);	/* back off a moment */
			continue;			/* and try again */
		}
		break;
	}
	return (kcid);
}

/*
 * If the current KCID is the same as kc->kc_chain_id, return 0;
 * if different, update the chain and return the new KCID.
 * This operation is non-destructive for unchanged kstats.
 */
kid_t
kstat_chain_update(kstat_ctl_t *kc)
{
	kstat_t k0, *headers, *oksp, *nksp, **okspp, *next;
	int i;
	kid_t kcid;
	kstat_t ksp = { 0 };

	kcid = (kid_t)kstat_ioctl(kc->kc_kd, KSTAT_IOC_CHAIN_ID, &ksp);
	if (kcid == -1)
		return (-1);
	if (kcid == kc->kc_chain_id)
		return (0);

	/*
	 * kstat 0's data is the kstat chain, so we can get the chain
	 * by doing a kstat_read() of this kstat.  The only fields the
	 * kstat driver needs are ks_kid (this identifies the kstat),
	 * ks_data (the pointer to our buffer), and ks_data_size (the
	 * size of our buffer).  By zeroing the struct we set ks_data = NULL
	 * and ks_data_size = 0, so that kstat_read() will automatically
	 * determine the size and allocate space for us.  We also fill in the
	 * name, so that truss can print something meaningful.
	 */
	bzero(&k0, sizeof (k0));
	(void) strlcpy(k0.ks_name, "kstat_headers", KSTAT_STRLEN);

	kcid = kstat_read(kc, &k0, NULL);
	if (kcid == -1) {
		free(k0.ks_data);
		/* errno set by kstat_read() */
		return (-1);
	}
	headers = k0.ks_data;

	/*
	 * Chain the new headers together
	 */
	for (i = 1; i < k0.ks_ndata; i++)
		headers[i - 1].ks_next = &headers[i];

	headers[k0.ks_ndata - 1].ks_next = NULL;

	/*
	 * Remove all deleted kstats from the chain.
	 */
	nksp = headers;
	okspp = &kc->kc_chain;
	oksp = kc->kc_chain;
	while (oksp != NULL) {
		next = oksp->ks_next;
		if (nksp != NULL && oksp->ks_kid == nksp->ks_kid) {
			okspp = &oksp->ks_next;
			nksp = nksp->ks_next;
		} else {
			*okspp = oksp->ks_next;
			free(oksp->ks_data);
			free(oksp);
		}
		oksp = next;
	}

	/*
	 * Add all new kstats to the chain.
	 */
	while (nksp != NULL) {
		kstat_zalloc((void **)okspp, sizeof (kstat_t), 0);
		if ((oksp = *okspp) == NULL) {
			free(headers);
			return (-1);
		}
		*oksp = *nksp;
		okspp = &oksp->ks_next;
		oksp->ks_next = NULL;
		oksp->ks_data = NULL;
		nksp = nksp->ks_next;
	}

	free(headers);
	kc->kc_chain_id = kcid;
	return (kcid);
}

kstat_t *
kstat_lookup(kstat_ctl_t *kc, char *ks_module, int ks_instance, char *ks_name)
{
	kstat_t *ksp;

	for (ksp = kc->kc_chain; ksp; ksp = ksp->ks_next) {
		if ((ks_module == NULL ||
		    strcmp(ksp->ks_module, ks_module) == 0) &&
		    (ks_instance == -1 || ksp->ks_instance == ks_instance) &&
		    (ks_name == NULL || strcmp(ksp->ks_name, ks_name) == 0))
			return (ksp);
	}

	errno = ENOENT;
	return (NULL);
}

void *
kstat_data_lookup(kstat_t *ksp, char *name)
{
	int i, size;
	char *namep, *datap;

	switch (ksp->ks_type) {

	case KSTAT_TYPE_NAMED:
		size = sizeof (kstat_named_t);
		namep = KSTAT_NAMED_PTR(ksp)->name;
		break;

	case KSTAT_TYPE_TIMER:
		size = sizeof (kstat_timer_t);
		namep = KSTAT_TIMER_PTR(ksp)->name;
		break;

	default:
		errno = EINVAL;
		return (NULL);
	}

	datap = ksp->ks_data;
	for (i = 0; i < ksp->ks_ndata; i++) {
		if (strcmp(name, namep) == 0)
			return (datap);
		namep += size;
		datap += size;
	}
	errno = ENOENT;
	return (NULL);
}
