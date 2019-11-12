dnl #
dnl # 3.1 API Change
dnl #
dnl # The rw_semaphore.wait_lock member was changed from spinlock_t to
dnl # raw_spinlock_t at commit ddb6c9b58a19edcfac93ac670b066c836ff729f1.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_RWSEM_SPINLOCK_IS_RAW], [
	ZFS_LINUX_TEST_SRC([rwsem_spinlock_is_raw], [
		#include <linux/rwsem.h>
	],[
		struct rw_semaphore dummy_semaphore __attribute__ ((unused));
		raw_spinlock_t dummy_lock __attribute__ ((unused)) =
		    __RAW_SPIN_LOCK_INITIALIZER(dummy_lock);
		dummy_semaphore.wait_lock = dummy_lock;
	])
])

AC_DEFUN([ZFS_AC_KERNEL_RWSEM_SPINLOCK_IS_RAW], [
	AC_MSG_CHECKING([whether struct rw_semaphore member wait_lock is raw])
	ZFS_LINUX_TEST_RESULT([rwsem_spinlock_is_raw], [
		AC_MSG_RESULT(yes)
	],[
		ZFS_LINUX_TEST_ERROR([rwsem_spinlock_is_raw])
	])
])

dnl #
dnl # 3.16 API Change
dnl #
dnl # rwsem-spinlock "->activity" changed to "->count"
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_RWSEM_ACTIVITY], [
	ZFS_LINUX_TEST_SRC([rwsem_activity], [
		#include <linux/rwsem.h>
	],[
		struct rw_semaphore dummy_semaphore __attribute__ ((unused));
		dummy_semaphore.activity = 0;
	])
])

AC_DEFUN([ZFS_AC_KERNEL_RWSEM_ACTIVITY], [
	AC_MSG_CHECKING([whether struct rw_semaphore has member activity])
	ZFS_LINUX_TEST_RESULT([rwsem_activity], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_RWSEM_ACTIVITY, 1,
		    [struct rw_semaphore has member activity])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 4.8 API Change
dnl #
dnl # rwsem "->count" changed to atomic_long_t type
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_RWSEM_ATOMIC_LONG_COUNT], [
	ZFS_LINUX_TEST_SRC([rwsem_atomic_long_count], [
		#include <linux/rwsem.h>
	],[
		DECLARE_RWSEM(dummy_semaphore);
		(void) atomic_long_read(&dummy_semaphore.count);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_RWSEM_ATOMIC_LONG_COUNT], [
	AC_MSG_CHECKING(
	    [whether struct rw_semaphore has atomic_long_t member count])
	ZFS_LINUX_TEST_RESULT([rwsem_atomic_long_count], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_RWSEM_ATOMIC_LONG_COUNT, 1,
		    [struct rw_semaphore has atomic_long_t member count])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_RWSEM], [
	ZFS_AC_KERNEL_SRC_RWSEM_SPINLOCK_IS_RAW
	ZFS_AC_KERNEL_SRC_RWSEM_ACTIVITY
	ZFS_AC_KERNEL_SRC_RWSEM_ATOMIC_LONG_COUNT
])

AC_DEFUN([ZFS_AC_KERNEL_RWSEM], [
	ZFS_AC_KERNEL_RWSEM_SPINLOCK_IS_RAW
	ZFS_AC_KERNEL_RWSEM_ACTIVITY
	ZFS_AC_KERNEL_RWSEM_ATOMIC_LONG_COUNT
])
