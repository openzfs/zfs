AC_DEFUN([ZFS_AC_CONFIG_USER_PYTHON_VERSIONS], [
	AC_MSG_CHECKING([version of python for arc_summary.py])

	# Always choose python3 if available
	if test -x /usr/bin/python3
	then
		AC_MSG_RESULT(3.x)
		python_version=3.x
	else
		# /usr/bin/python must be python2. It should be since a python3
		# binary is supposed to be mandatory but we still check.
		python_version=`/usr/bin/python -c "import sys; print (sys.version.split()[[0]])" 2> /dev/null`
		if test $? -gt 0
		then
			AC_MSG_RESULT(unable to determine)
		elif test "${python_version:0:2}" = "2."
		then
			AC_MSG_RESULT(2.x)
		else
			AC_MSG_RESULT(unusable, not installing)
		fi

	fi

	AM_CONDITIONAL([HAVE_PYTHON_2], [test "${python_version:0:2}" = "2."])
	AM_CONDITIONAL([HAVE_PYTHON_3], [test "${python_version:0:2}" = "3."])

])
