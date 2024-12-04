dnl #
dnl # 6.10 kernel, check number of args of __assign_str() for trace:
dnl
dnl # 6.10+:           one arg
dnl # 6.9 and older:   two args
dnl #
dnl # More specifically, this will test to see if __assign_str() takes one
dnl # arg.  If __assign_str() takes two args, or is not defined, then
dnl # HAVE_1ARG_ASSIGN_STR will not be set.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_1ARG_ASSIGN_STR], [
       AC_MSG_CHECKING([whether __assign_str() has one arg])
       ZFS_LINUX_TRY_COMPILE_HEADER([
               #include <linux/module.h>
               MODULE_LICENSE("$ZFS_META_LICENSE");

               #define CREATE_TRACE_POINTS
               #include "conftest.h"
       ],[
               trace_zfs_autoconf_event_one("1");
               trace_zfs_autoconf_event_two("2");
       ],[
               AC_MSG_RESULT(yes)
               AC_DEFINE(HAVE_1ARG_ASSIGN_STR, 1,
                   [__assign_str() has one arg])
       ],[
               AC_MSG_RESULT(no)
       ],[
               #if !defined(_CONFTEST_H) || defined(TRACE_HEADER_MULTI_READ)
               #define _CONFTEST_H

               #undef  TRACE_SYSTEM
               #define TRACE_SYSTEM zfs
               #include <linux/tracepoint.h>

               DECLARE_EVENT_CLASS(zfs_autoconf_event_class,
                       TP_PROTO(char *string),
                       TP_ARGS(string),
                       TP_STRUCT__entry(
                               __string(str, string)
                       ),
                       TP_fast_assign(
                               __assign_str(str);
                       ),
                       TP_printk("str = %s", __get_str(str))
               );

               #define DEFINE_AUTOCONF_EVENT(name) \
               DEFINE_EVENT(zfs_autoconf_event_class, name, \
                       TP_PROTO(char * str), \
                       TP_ARGS(str))
               DEFINE_AUTOCONF_EVENT(zfs_autoconf_event_one);
               DEFINE_AUTOCONF_EVENT(zfs_autoconf_event_two);

               #endif /* _CONFTEST_H */

               #undef  TRACE_INCLUDE_PATH
               #define TRACE_INCLUDE_PATH .
               #define TRACE_INCLUDE_FILE conftest
               #include <trace/define_trace.h>
       ])
])
