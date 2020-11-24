dnl #
dnl # get_user_pages_unlocked() function was not available till 4.0.
dnl # In earlier kernels (< 4.0) get_user_pages() is available().
dnl #
dnl # 4.0 API change,
dnl # long get_user_pages_unlocked(struct task_struct *tsk,
dnl #       struct mm_struct *mm, unsigned long start, unsigned long nr_pages,
dnl #       int write, int force, struct page **pages)
dnl #
dnl # 4.8 API change,
dnl # long get_user_pages_unlocked(unsigned long start,
dnl #       unsigned long nr_pages, int write, int force, struct page **page)
dnl #
dnl # 4.9 API change,
dnl # long get_user_pages_unlocked(usigned long start, int nr_pages,
dnl #       struct page **pages, unsigned int gup_flags)
dnl #

dnl#
dnl# Check available get_user_pages/_unlocked interfaces.
dnl#
AC_DEFUN([ZFS_AC_KERNEL_SRC_GET_USER_PAGES], [
	ZFS_LINUX_TEST_SRC([get_user_pages_unlocked_gup_flags], [
		#include <linux/mm.h>
	], [
		unsigned long start = 0;
		unsigned long nr_pages = 1;
		unsigned int gup_flags = 0;
		struct page **pages = NULL;
		long ret __attribute__ ((unused));

		ret = get_user_pages_unlocked(start, nr_pages, pages,
		    gup_flags);
	])

	ZFS_LINUX_TEST_SRC([get_user_pages_unlocked_write_flag], [
		#include <linux/mm.h>
	], [
		unsigned long start = 0;
		unsigned long nr_pages = 1;
		int write = 0;
		int force = 0;
		long ret __attribute__ ((unused));
		struct page **pages = NULL;

		ret = get_user_pages_unlocked(start, nr_pages, write, force,
		    pages);
	])

	ZFS_LINUX_TEST_SRC([get_user_pages_unlocked_task_struct], [
		#include <linux/mm.h>
	], [
		struct task_struct *tsk = NULL;
		struct mm_struct *mm = NULL;
		unsigned long start = 0;
		unsigned long nr_pages = 1;
		int write = 0;
		int force = 0;
		struct page **pages = NULL;
		long ret __attribute__ ((unused));

		ret = get_user_pages_unlocked(tsk, mm, start, nr_pages, write,
		    force, pages);
	])

	ZFS_LINUX_TEST_SRC([get_user_pages_unlocked_task_struct_gup_flags], [
		#include <linux/mm.h>
	], [
		struct task_struct *tsk = NULL;
		struct mm_struct *mm = NULL;
		unsigned long start = 0;
		unsigned long nr_pages = 1;
		struct page **pages = NULL;
		unsigned int gup_flags = 0;
		long ret __attribute__ ((unused));

		ret = get_user_pages_unlocked(tsk, mm, start, nr_pages,
		    pages, gup_flags);
	])

	ZFS_LINUX_TEST_SRC([get_user_pages_task_struct], [
		#include <linux/mm.h>
	], [
		struct task_struct *tsk = NULL;
		struct mm_struct *mm = NULL;
		struct vm_area_struct **vmas = NULL;
		unsigned long start = 0;
		unsigned long nr_pages = 1;
		int write = 0;
		int force = 0;
		struct page **pages = NULL;
		int ret __attribute__ ((unused));

		ret = get_user_pages(tsk, mm, start, nr_pages, write,
		    force, pages, vmas);
	])
])

dnl #
dnl # Supported get_user_pages/_unlocked interfaces checked newest to oldest.
dnl # We first check for get_user_pages_unlocked as that is available in
dnl # newer kernels.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_GET_USER_PAGES], [
	dnl #
	dnl # Current API (as of 4.9) of get_user_pages_unlocked
	dnl #
	AC_MSG_CHECKING([whether get_user_pages_unlocked() takes gup flags])
	ZFS_LINUX_TEST_RESULT([get_user_pages_unlocked_gup_flags], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GET_USER_PAGES_UNLOCKED_GUP_FLAGS, 1,
		    [get_user_pages_unlocked() takes gup flags])
	], [
		AC_MSG_RESULT(no)

		dnl #
		dnl # 4.8 API change, get_user_pages_unlocked
		dnl #
		AC_MSG_CHECKING(
		    [whether get_user_pages_unlocked() takes write flag])
		ZFS_LINUX_TEST_RESULT([get_user_pages_unlocked_write_flag], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_GET_USER_PAGES_UNLOCKED_WRITE_FLAG, 1,
			    [get_user_pages_unlocked() takes write flag])
		], [
			AC_MSG_RESULT(no)

			dnl #
			dnl # 4.0-4.3, 4.5-4.7 API, get_user_pages_unlocked
			dnl #
			AC_MSG_CHECKING(
			    [whether get_user_pages_unlocked() takes task_struct])
			ZFS_LINUX_TEST_RESULT(
			    [get_user_pages_unlocked_task_struct], [
				AC_MSG_RESULT(yes)
				AC_DEFINE(
				    HAVE_GET_USER_PAGES_UNLOCKED_TASK_STRUCT, 1,
				    [get_user_pages_unlocked() takes task_struct])
			], [
				AC_MSG_RESULT(no)

				dnl #
				dnl # 4.4 API, get_user_pages_unlocked
				dnl #
				AC_MSG_CHECKING(
				    [whether get_user_pages_unlocked() takes task_struct, gup_flags])
				ZFS_LINUX_TEST_RESULT(
				    [get_user_pages_unlocked_task_struct_gup_flags], [
					AC_MSG_RESULT(yes)
					AC_DEFINE(
					    HAVE_GET_USER_PAGES_UNLOCKED_TASK_STRUCT_GUP_FLAGS, 1,
					    [get_user_pages_unlocked() takes task_struct, gup_flags])
				], [
					AC_MSG_RESULT(no)

					dnl #
					dnl # get_user_pages
					dnl #
					AC_MSG_CHECKING(
					    [whether get_user_pages() takes struct task_struct])
					ZFS_LINUX_TEST_RESULT(
					    [get_user_pages_task_struct], [
						AC_MSG_RESULT(yes)
						AC_DEFINE(
						    HAVE_GET_USER_PAGES_TASK_STRUCT, 1,
						    [get_user_pages() takes task_struct])
					], [
						dnl #
						dnl # If we cannot map the user's
						dnl # pages in then we cannot do
						dnl # Direct I/O
						dnl #
						ZFS_LINUX_TEST_ERROR([Direct I/O])
					])
				])
			])
		])
	])
])
