// SPDX-License-Identifier: BSD-2-Clause
#include <sys/zfs_racct.h>

void
zfs_racct_read(spa_t *spa, uint64_t size, uint64_t iops, dmu_flags_t flags)
{
	spa_iostats_read_add(spa, size, iops, flags);
}

void
zfs_racct_write(spa_t *spa, uint64_t size, uint64_t iops, dmu_flags_t flags)
{
	spa_iostats_write_add(spa, size, iops, flags);
}
