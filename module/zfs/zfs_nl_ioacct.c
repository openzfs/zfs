#include "sys/zfs_nl_ioacct.h"

struct sock *nl_sk = NULL;

void
serialize_io_info(const zfs_io_info_t *zii, nl_msg *io_msg)
{
	nl_msg *seeker = io_msg;
	memcpy(seeker, &zii->pid, sizeof (pid_t));
	seeker += sizeof (pid_t);
	memcpy(seeker, &zii->nbytes, sizeof (ssize_t));
	seeker += sizeof (ssize_t);
	memcpy(seeker, &zii->op, sizeof (zfs_io_type_t));
	seeker += sizeof (zfs_io_type_t);
	memcpy(seeker, &zii->fsname, ZFS_MAXNAMELEN);
}

void
zfs_nl_ioacct_send(zfs_io_info_t *zii)
{
	size_t nl_msglen;
	struct nlmsghdr *nl_header;
	struct sk_buff *skb;
	nl_msg *io_msg;

	nl_msglen = NETLINK_MSGLEN;

	io_msg = kmalloc(nl_msglen, GFP_KERNEL);
	if (!io_msg)
		goto out;

	serialize_io_info(zii, io_msg);

	skb = nlmsg_new(nl_msglen, GFP_KERNEL);
	if (!skb)
		goto out;

	nl_header = nlmsg_put(skb, 0, 0, NLMSG_DONE, nl_msglen, 0);
	if (!nl_header)
		goto out;

	memcpy(nlmsg_data(nl_header), io_msg, nl_msglen);
	nlmsg_multicast(nl_sk, skb, 0, ZFS_NL_IO_GRP, 0);

out:
	kfree(io_msg);
}

static int
zfs_nl_ioacct_netlink_init(void)
{
	nl_sk = netlink_kernel_create(&init_net, ZFS_NL_IO_PROTO, NULL);
	if (!nl_sk)
		return (-1);

	return (0);
}

int
zfs_nl_ioacct_init(void)
{
	int netlink_id;

	printk(KERN_INFO "ZFS: netlink ioacct: initializing\n");

	netlink_id = zfs_nl_ioacct_netlink_init();
	if (netlink_id < 0) {
		printk(KERN_ERR
			"ZFS: netlink ioacct: error creating socket.\n");
		return (netlink_id);
	}

	printk(KERN_INFO "ZFS: netlink ioacct: initialized\n");

	return (0);
}

void
zfs_nl_ioacct_fini(void)
{
	netlink_kernel_release(nl_sk);
}
