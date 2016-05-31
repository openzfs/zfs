#include "libioacct.h"

void
deserialize_io_info(zfs_io_info_t *zii, nl_msg *io_msg)
{
	nl_msg *seeker = io_msg;

	memcpy(&zii->pid, seeker, sizeof (pid_t));
	seeker += sizeof (pid_t);
	memcpy(&zii->nbytes, seeker, sizeof (ssize_t));
	seeker += sizeof (ssize_t);
	memcpy(&zii->op, seeker, sizeof (zfs_io_type_t));
	seeker += sizeof (zfs_io_type_t);
	memcpy(&zii->fsname, seeker, ZFS_MAXNAMELEN);
}
