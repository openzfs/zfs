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
*
* Copyright (C) 2017 Jorgen Lundman <lundman@lundman.net>
*
*/

/*
* Provides an implementation of the union of Illumos and OSX UIO struct 
* and API calls. That is to say the OsX API calls are kept, to keep
* the UIO structure as opaque, but the internals are more like Illumos
* to avoid the OsX 32bit vs 64bit logic.
*/

#include <sys/uio.h>


uio_t *uio_create(int iovcount, off_t offset, int spacetype, int iodirection)
{
	void *                          my_buf_p;
	uint64_t                        my_size;
	uio_t                          *my_uio;

	// Future, make sure the uio struct is aligned, and do one alloc for uio and iovec
	my_size = sizeof(uio_t);
	my_uio = kmem_alloc((uint32_t)my_size, KM_SLEEP);

	memset(my_uio, 0, my_size);
	//my_uio->uio_size = my_size;
	my_uio->uio_segflg = spacetype;

	if (iovcount > 0) {
		my_uio->uio_iov = kmem_alloc(iovcount * sizeof(iovec_t), KM_SLEEP);
		memset(my_uio->uio_iov, 0, iovcount * sizeof(iovec_t));
	}
	else {
		my_uio->uio_iov = NULL;
	}
	my_uio->uio_max_iovs = iovcount;
	my_uio->uio_offset = offset;
	my_uio->uio_rw = iodirection;

	return (my_uio);
}

void uio_free(uio_t *uio)
{
	ASSERT(uio != NULL);
	ASSERT(uio->uio_iov != NULL);

	kmem_free(uio->uio_iov, uio->uio_max_iovs * sizeof(iovec_t));
	kmem_free(uio, sizeof(uio_t));

}

int uio_addiov(uio_t *uio, user_addr_t baseaddr, user_size_t length)
{
	ASSERT(uio != NULL);
	ASSERT(uio->uio_iov != NULL);

	for (int i = 0; i < uio->uio_max_iovs; i++) {
		if (uio->uio_iov[i].iov_len == 0 && uio->uio_iov[i].iov_base == 0) {
			uio->uio_iov[i].iov_len = (uint64_t)length;
			uio->uio_iov[i].iov_base = (void *)(user_addr_t)baseaddr;
			uio->uio_iovcnt++;
			uio->uio_resid += length;
			return(0);
		}
	}

	return(-1);
}

int uio_isuserspace(uio_t *uio)
{
	ASSERT(uio != NULL);
	if (uio->uio_segflg == UIO_USERSPACE)
		return 1;
	return 0;
}

int uio_getiov(uio_t *uio, int index, user_addr_t *baseaddr, user_size_t *length)
{
	ASSERT(uio != NULL);
	ASSERT(uio->uio_iov != NULL);

	if (index < 0 || index >= uio->uio_iovcnt) {
		return(-1);
	}

	if (baseaddr != NULL) {
		*baseaddr = (user_addr_t) uio->uio_iov[index].iov_base;
	}
	if (length != NULL) {
		*length = uio->uio_iov[index].iov_len;
	}

	return 0;
}

int uio_iovcnt(uio_t *uio)
{
	if (uio == NULL) {
		return(0);
	}

	return(uio->uio_iovcnt);
}


off_t uio_offset(uio_t *uio) 
{
	ASSERT(uio != NULL);
	ASSERT(uio->uio_iov != NULL);

	if (uio == NULL) {
		return(0);
	}

	return(uio->uio_offset);
}

/*
 * This function is modelled after OsX, which means you can only pass
 * in a value between 0 and current "iov_len". Any larger number will
 * ignore the extra bytes.
*/
void uio_update(uio_t *uio, user_size_t count)
{
	uint32_t ind;

	if (uio == NULL || uio->uio_iovcnt < 1) {
		return;
	}

	ASSERT(uio->uio_index < uio->uio_iovcnt);

	ind = uio->uio_index;

	if (count) {
		if (count > uio->uio_iov->iov_len) {
			(uintptr_t)uio->uio_iov[ind].iov_base += uio->uio_iov[ind].iov_len;
			uio->uio_iov[ind].iov_len = 0;
		}
		else {
			(uintptr_t)uio->uio_iov[ind].iov_base += count;
			uio->uio_iov[ind].iov_len -= count;
		}
		if (count > (user_size_t)uio->uio_resid) {
			uio->uio_offset += uio->uio_resid;
			uio->uio_resid = 0;
		}
		else {
			uio->uio_offset += count;
			uio->uio_resid -= count;
		}
	}

	while (uio->uio_iovcnt > 0 && uio->uio_iov[ind].iov_len == 0) {
		uio->uio_iovcnt--;
		if (uio->uio_iovcnt > 0) {
			uio->uio_index = (ind++);
		}
	}
}


uint64_t uio_resid(uio_t *uio)
{
	if (uio == NULL) {
		return(0);
	}

	return(uio->uio_resid);
}

user_addr_t uio_curriovbase(uio_t *uio)
{
	if (uio == NULL || uio->uio_iovcnt < 1) {
		return(0);
	}

	return((user_addr_t)uio->uio_iov[uio->uio_index].iov_base);
}

user_size_t uio_curriovlen(uio_t *a_uio)
{
	if (a_uio == NULL || a_uio->uio_iovcnt < 1) {
		return(0);
	}

	return((user_size_t)a_uio->uio_iov[a_uio->uio_index].iov_len);
}

void uio_setoffset(uio_t *uio, off_t offset)
{
	if (uio == NULL) {
		return;
	}
	uio->uio_offset = offset;
}

int uio_rw(uio_t *a_uio)
{
	if (a_uio == NULL) {
		return(-1);
	}
	return(a_uio->uio_rw);
}

void uio_setrw(uio_t *a_uio, int a_value)
{
	if (a_uio == NULL) {
		return;
	}

	if (a_value == UIO_READ || a_value == UIO_WRITE) {
		a_uio->uio_rw = a_value;
	}
	return;
}

int uio_spacetype(uio_t *a_uio)
{
	if (a_uio == NULL) {
		return(-1);
	}

	return(a_uio->uio_segflg);
}


uio_t *uio_duplicate(uio_t *a_uio)
{
	uio_t           *my_uio;
	int                     i;

	if (a_uio == NULL) {
		return(NULL);
	}

	my_uio = uio_create(a_uio->uio_max_iovs,
		uio_offset(a_uio),
		uio_spacetype(a_uio),
		uio_rw(a_uio));
	if (my_uio == 0) {
		panic("%s :%d - allocation failed\n", __FILE__, __LINE__);
	}

	bcopy((void *)a_uio->uio_iov, (void *)my_uio->uio_iov, a_uio->uio_max_iovs * sizeof(iovec_t));
	my_uio->uio_index = a_uio->uio_index;
	my_uio->uio_resid = a_uio->uio_resid;
	my_uio->uio_iovcnt = a_uio->uio_iovcnt;

	return(my_uio);
}

int spl_uiomove(const uint8_t *c_cp, uint32_t n, struct uio *uio)
{
	const uint8_t *cp = c_cp;
	uint64_t acnt;
	int error = 0;

	while (n > 0 && uio_resid(uio)) {
		uio_update(uio, 0);
		acnt = uio_curriovlen(uio);
		if (acnt == 0) {
			continue;
		}
		if (n > 0 && acnt > (uint64_t)n)
			acnt = n;

		switch ((int)uio->uio_segflg) {
		case UIO_SYSSPACE:
			if (uio->uio_rw == UIO_READ)
				/*error =*/ bcopy(cp, uio->uio_iov[uio->uio_index].iov_base,
					acnt);
			else
				/*error =*/ bcopy(uio->uio_iov[uio->uio_index].iov_base, (void *)cp,
					acnt);
			break;
		default:
			break;
		}
		uio_update(uio, acnt);
		cp += acnt;
		n -= (uint32_t)acnt;
	}
	ASSERT0(n);
	return (error);
}

