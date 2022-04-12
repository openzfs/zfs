/*
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  Solaris Porting Layer (SPL) Automount expiration Implementation.
 */

#include <sys/mount.h>

static LIST_HEAD(spl_automount_list);

static void
spl_mount_expire(struct work_struct *data);
static DECLARE_DELAYED_WORK(spl_automount_task, spl_mount_expire);

static int last_delay_in_seconds = 0;

static void
spl_mount_expire(struct work_struct *data)
{
	struct list_head *list = &spl_automount_list;
	printk("performing mount expiry expire");
	mark_mounts_for_expiry(list);
	if (!list_empty(list)) {
		printk("Rescheduling in %d seconds", last_delay_in_seconds);
		schedule_delayed_work(&spl_automount_task,
		    READ_ONCE(last_delay_in_seconds) * HZ);
	} else {
		printk("No need for rescheduling.");
	}
}

void
spl_add_mount_to_expire(spl_mount *mnt, int delay_in_seconds)
{
	printk("scheduling! expire in %d", delay_in_seconds);
	if (last_delay_in_seconds != delay_in_seconds) {
		cancel_delayed_work(&spl_automount_task);
	}
	last_delay_in_seconds = delay_in_seconds;
	mnt_set_expiry(mnt, &spl_automount_list);
	printk("scheduling expire in %d", delay_in_seconds);
	schedule_delayed_work(&spl_automount_task, delay_in_seconds * HZ);
}
EXPORT_SYMBOL(spl_add_mount_to_expire);
