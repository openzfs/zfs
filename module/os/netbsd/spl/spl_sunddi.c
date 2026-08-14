// SPDX-License-Identifier: BSD-2-Clause
#include <sys/types.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/libkern.h>
#include <sys/limits.h>
#include <sys/misc.h>
#include <sys/sunddi.h>
#include <sys/sysctl.h>
#include <sys/kmem.h>
#include <sys/mutex.h>


int
ddi_strtol(const char *str, char **nptr, int base, long *result)
{

	*result = (long)strtoll(str, nptr, base);
	return (0);
}

int
ddi_strtoul(const char *str, char **nptr, int base, unsigned long *result)
{

	*result = strtoul(str, nptr, base);
	return (0);
}

int
ddi_strtoull(const char *str, char **nptr, int base, unsigned long long *result)
{

	*result = strtoull(str, nptr, base);
	return (0);
}

int
ddi_strtoll(const char *str, char **nptr, int base, longlong_t *result)
{

	*result = (longlong_t)strtoll(str, nptr, base);
	return (0);
}

int
ddi_ffs(long mask)
{
	if (mask == 0)
		return (0);
	return (ffs((int)mask));
}

int
ddi_fls(long mask)
{
	int bit;

	if (mask == 0)
		return (0);
	bit = 1;
	while (mask >>= 1)
		bit++;
	return (bit);
}

struct ddi_soft_state {
	size_t		ss_size;
	size_t		ss_nitems;
	void		**ss_array;
	kmutex_t	ss_lock;
};

int
ddi_soft_state_init(void **statep, size_t size, size_t nitems)
{
	struct ddi_soft_state *ss;

	if (nitems == 0)
		nitems = 1;
	ss = kmem_zalloc(sizeof (*ss), KM_SLEEP);
	ss->ss_size = size;
	ss->ss_nitems = nitems;
	ss->ss_array = kmem_zalloc(nitems * sizeof (void *), KM_SLEEP);
	mutex_init(&ss->ss_lock, NULL, MUTEX_DEFAULT, NULL);
	*statep = ss;
	return (0);
}

void
ddi_soft_state_fini(void **statep)
{
	struct ddi_soft_state *ss = *statep;
	size_t i;

	if (ss == NULL)
		return;
	for (i = 0; i < ss->ss_nitems; i++) {
		if (ss->ss_array[i] != NULL)
			kmem_free(ss->ss_array[i], ss->ss_size);
	}
	kmem_free(ss->ss_array, ss->ss_nitems * sizeof (void *));
	mutex_destroy(&ss->ss_lock);
	kmem_free(ss, sizeof (*ss));
	*statep = NULL;
}

void *
ddi_get_soft_state(void *state, int item)
{
	struct ddi_soft_state *ss = state;

	if (ss == NULL || item < 0 || (size_t)item >= ss->ss_nitems)
		return (NULL);
	return (ss->ss_array[item]);
}

int
ddi_soft_state_zalloc(void *state, int item)
{
	struct ddi_soft_state *ss = state;
	void *p;

	if (ss == NULL || item < 0 || (size_t)item >= ss->ss_nitems)
		return (DDI_FAILURE);
	if (ss->ss_array[item] != NULL)
		return (DDI_FAILURE);
	p = kmem_zalloc(ss->ss_size, KM_SLEEP);
	ss->ss_array[item] = p;
	return (DDI_SUCCESS);
}

void
ddi_soft_state_free(void *state, int item)
{
	struct ddi_soft_state *ss = state;

	if (ss == NULL || item < 0 || (size_t)item >= ss->ss_nitems)
		return;
	if (ss->ss_array[item] != NULL) {
		kmem_free(ss->ss_array[item], ss->ss_size);
		ss->ss_array[item] = NULL;
	}
}
