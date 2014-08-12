dnl #
dnl # 3.4 API Change
dnl # Added truncate_pagecache_range() function.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_TRUNCATE_PAGECACHE_RANGE],
	[AC_MSG_CHECKING([whether truncate_pagecache_range() is available])
	ZFS_LINUX_TRY_COMPILE_SYMBOL([
		#include <linux/mm.h>
	], [
		truncate_pagecache_range(NULL, 0, 0);
	], [truncate_pagecache_range], [mm/truncate.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_TRUNCATE_PAGECACHE_RANGE, 1,
		          [truncate_pagecache_range() is available])
	], [
		AC_MSG_RESULT(no)
	])
])
