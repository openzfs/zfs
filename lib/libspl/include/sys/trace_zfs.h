/* Here to keep the libspl build happy */

#ifndef _LIBSPL_ZFS_TRACE_H
#define	_LIBSPL_ZFS_TRACE_H

/*
 * The set-error SDT probe is extra static, in that we declare its fake
 * function literally, rather than with the DTRACE_PROBE1() macro.  This is
 * necessary so that SET_ERROR() can evaluate to a value, which wouldn't
 * be possible if it required multiple statements (to declare the function
 * and then call it).
 *
 * SET_ERROR() uses the comma operator so that it can be used without much
 * additional code.  For example, "return (EINVAL);" becomes
 * "return (SET_ERROR(EINVAL));".  Note that the argument will be evaluated
 * twice, so it should not have side effects (e.g. something like:
 * "return (SET_ERROR(log_error(EINVAL, info)));" would log the error twice).
 */
#undef SET_ERROR
#define	SET_ERROR(err) \
	(__set_error(__FILE__, __func__, __LINE__, err), err)


#endif
