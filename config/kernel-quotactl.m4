dnl # SPDX-License-Identifier: CDDL-1.0
dnl #
dnl # Linux 4.1 API
dnl # quotactl_ops with qc_dqblk
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_QUOTACTL], [
	ZFS_LINUX_TEST_SRC([quotactl_ops], [
		#include <linux/fs.h>
		#include <linux/quota.h>
		#include <linux/quotaops.h>

		static int test_get_dqblk(struct super_block *sb,
		    struct kqid qid, struct qc_dqblk *fdq) {
			qid_t id = from_kqid(sb->s_user_ns, qid);
			(void) id;
			(void) fdq;
			return (0);
		}
		static int test_set_dqblk(struct super_block *sb,
		    struct kqid qid, struct qc_dqblk *fdq) {
			return (0);
		}
		static int test_get_state(struct super_block *sb,
		    struct qc_state *state) {
			return (0);
		}

		static const struct quotactl_ops
		    test_qcop __attribute__((unused)) = {
			.get_dqblk	= test_get_dqblk,
			.set_dqblk	= test_set_dqblk,
			.get_state	= test_get_state,
		};
	], [
		struct super_block sb;
		sb.s_qcop = &test_qcop;
	])

	ZFS_LINUX_TEST_SRC([super_block_s_quota_types], [
		#include <linux/fs.h>
		#include <linux/quota.h>
	], [
		struct super_block sb;
		sb.s_quota_types = QTYPE_MASK_USR | QTYPE_MASK_GRP;
	])
])

AC_DEFUN([ZFS_AC_KERNEL_QUOTACTL], [
	AC_MSG_CHECKING([whether super_block has s_qcop with qc_dqblk])
	ZFS_LINUX_TEST_RESULT([quotactl_ops], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_QUOTACTL_OPS, 1,
		    [super_block has s_qcop with qc_dqblk])
	], [
		AC_MSG_RESULT([no])
	])

	AC_MSG_CHECKING([whether super_block has s_quota_types])
	ZFS_LINUX_TEST_RESULT([super_block_s_quota_types], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_SUPER_BLOCK_S_QUOTA_TYPES, 1,
		    [super_block has s_quota_types])
	], [
		AC_MSG_RESULT([no])
	])
])
