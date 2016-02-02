dnl #
dnl # 3.6 API change,
dnl # sg_alloc_table_from_pages, allows merging adjacent pages
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SG_FROM_PAGES], [
	AC_MSG_CHECKING([whether sg_alloc_table_from_pages exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/scatterlist.h>
		#define NR 4
	],[
		struct sg_table sgt;
		struct page *pages[NR];
		int ret;
		ret = sg_alloc_table_from_pages(&sgt, pages, NR, 0, NR*PAGE_SIZE, GFP_KERNEL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SG_FROM_PAGES, 1, [sg_alloc_table_from_pages])
	],[
		AC_MSG_RESULT(no)
	])
])

