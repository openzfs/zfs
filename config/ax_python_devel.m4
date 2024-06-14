# ===========================================================================
#     https://www.gnu.org/software/autoconf-archive/ax_python_devel.html
# ===========================================================================
#
# SYNOPSIS
#
#   AX_PYTHON_DEVEL([version[,optional]])
#
# DESCRIPTION
#
#   Note: Defines as a precious variable "PYTHON_VERSION". Don't override it
#   in your configure.ac.
#
#   This macro checks for Python and tries to get the include path to
#   'Python.h'. It provides the $(PYTHON_CPPFLAGS) and $(PYTHON_LIBS) output
#   variables. It also exports $(PYTHON_EXTRA_LIBS) and
#   $(PYTHON_EXTRA_LDFLAGS) for embedding Python in your code.
#
#   You can search for some particular version of Python by passing a
#   parameter to this macro, for example ">= '2.3.1'", or "== '2.4'". Please
#   note that you *have* to pass also an operator along with the version to
#   match, and pay special attention to the single quotes surrounding the
#   version number. Don't use "PYTHON_VERSION" for this: that environment
#   variable is declared as precious and thus reserved for the end-user.
#
#   By default this will fail if it does not detect a development version of
#   python.  If you want it to continue, set optional to true, like
#   AX_PYTHON_DEVEL([], [true]).  The ax_python_devel_found variable will be
#   "no" if it fails.
#
#   This macro should work for all versions of Python >= 2.1.0. As an end
#   user, you can disable the check for the python version by setting the
#   PYTHON_NOVERSIONCHECK environment variable to something else than the
#   empty string.
#
#   If you need to use this macro for an older Python version, please
#   contact the authors. We're always open for feedback.
#
# LICENSE
#
#   Copyright (c) 2009 Sebastian Huber <sebastian-huber@web.de>
#   Copyright (c) 2009 Alan W. Irwin
#   Copyright (c) 2009 Rafael Laboissiere <rafael@laboissiere.net>
#   Copyright (c) 2009 Andrew Collier
#   Copyright (c) 2009 Matteo Settenvini <matteo@member.fsf.org>
#   Copyright (c) 2009 Horst Knorr <hk_classes@knoda.org>
#   Copyright (c) 2013 Daniel Mullner <muellner@math.stanford.edu>
#
#   This program is free software: you can redistribute it and/or modify it
#   under the terms of the GNU General Public License as published by the
#   Free Software Foundation, either version 3 of the License, or (at your
#   option) any later version.
#
#   This program is distributed in the hope that it will be useful, but
#   WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
#   Public License for more details.
#
#   You should have received a copy of the GNU General Public License along
#   with this program. If not, see <https://www.gnu.org/licenses/>.
#
#   As a special exception, the respective Autoconf Macro's copyright owner
#   gives unlimited permission to copy, distribute and modify the configure
#   scripts that are the output of Autoconf when processing the Macro. You
#   need not follow the terms of the GNU General Public License when using
#   or distributing such scripts, even though portions of the text of the
#   Macro appear in them. The GNU General Public License (GPL) does govern
#   all other use of the material that constitutes the Autoconf Macro.
#
#   This special exception to the GPL applies to versions of the Autoconf
#   Macro released by the Autoconf Archive. When you make and distribute a
#   modified version of the Autoconf Macro, you may extend this special
#   exception to the GPL to apply to your modified version as well.

#serial 36

AU_ALIAS([AC_PYTHON_DEVEL], [AX_PYTHON_DEVEL])
AC_DEFUN([AX_PYTHON_DEVEL],[
	# Get whether it's optional
	if test -z "$2"; then
	   ax_python_devel_optional=false
	else
	   ax_python_devel_optional=$2
	fi
	ax_python_devel_found=yes

	#
	# Allow the use of a (user set) custom python version
	#
	AC_ARG_VAR([PYTHON_VERSION],[The installed Python
		version to use, for example '2.3'. This string
		will be appended to the Python interpreter
		canonical name.])

	AC_PATH_PROG([PYTHON],[python[$PYTHON_VERSION]])
	if test -z "$PYTHON"; then
	   AC_MSG_WARN([Cannot find python$PYTHON_VERSION in your system path])
	   if ! $ax_python_devel_optional; then
	      AC_MSG_ERROR([Giving up, python development not available])
	   fi
	   ax_python_devel_found=no
	   PYTHON_VERSION=""
	fi

	if test $ax_python_devel_found = yes; then
	   #
	   # Check for a version of Python >= 2.1.0
	   #
	   AC_MSG_CHECKING([for a version of Python >= '2.1.0'])
	   ac_supports_python_ver=`$PYTHON -c "import sys; \
		ver = sys.version.split ()[[0]]; \
		print (ver >= '2.1.0')"`
	   if test "$ac_supports_python_ver" != "True"; then
		if test -z "$PYTHON_NOVERSIONCHECK"; then
			AC_MSG_RESULT([no])
			AC_MSG_WARN([
This version of the AC@&t@_PYTHON_DEVEL macro
doesn't work properly with versions of Python before
2.1.0. You may need to re-run configure, setting the
variables PYTHON_CPPFLAGS, PYTHON_LIBS, PYTHON_SITE_PKG,
PYTHON_EXTRA_LIBS and PYTHON_EXTRA_LDFLAGS by hand.
Moreover, to disable this check, set PYTHON_NOVERSIONCHECK
to something else than an empty string.
])
			if ! $ax_python_devel_optional; then
			   AC_MSG_FAILURE([Giving up])
			fi
			ax_python_devel_found=no
			PYTHON_VERSION=""
		else
			AC_MSG_RESULT([skip at user request])
		fi
	   else
		AC_MSG_RESULT([yes])
	   fi
	fi

	if test $ax_python_devel_found = yes; then
	   #
	   # If the macro parameter ``version'' is set, honour it.
	   # A Python shim class, VPy, is used to implement correct version comparisons via
	   # string expressions, since e.g. a naive textual ">= 2.7.3" won't work for
	   # Python 2.7.10 (the ".1" being evaluated as less than ".3").
	   #
	   if test -n "$1"; then
		AC_MSG_CHECKING([for a version of Python $1])
                cat << EOF > ax_python_devel_vpy.py
class VPy:
    def vtup(self, s):
        return tuple(map(int, s.strip().replace("rc", ".").split(".")))
    def __init__(self):
        import sys
        self.vpy = tuple(sys.version_info)[[:3]]
    def __eq__(self, s):
        return self.vpy == self.vtup(s)
    def __ne__(self, s):
        return self.vpy != self.vtup(s)
    def __lt__(self, s):
        return self.vpy < self.vtup(s)
    def __gt__(self, s):
        return self.vpy > self.vtup(s)
    def __le__(self, s):
        return self.vpy <= self.vtup(s)
    def __ge__(self, s):
        return self.vpy >= self.vtup(s)
EOF
		ac_supports_python_ver=`$PYTHON -c "import ax_python_devel_vpy; \
                        ver = ax_python_devel_vpy.VPy(); \
			print (ver $1)"`
                rm -rf ax_python_devel_vpy*.py* __pycache__/ax_python_devel_vpy*.py*
		if test "$ac_supports_python_ver" = "True"; then
			AC_MSG_RESULT([yes])
		else
			AC_MSG_RESULT([no])
			AC_MSG_WARN([this package requires Python $1.
If you have it installed, but it isn't the default Python
interpreter in your system path, please pass the PYTHON_VERSION
variable to configure. See ``configure --help'' for reference.
])
			if ! $ax_python_devel_optional; then
			   AC_MSG_ERROR([Giving up])
			fi
			ax_python_devel_found=no
			PYTHON_VERSION=""
		fi
	   fi
	fi

	if test $ax_python_devel_found = yes; then
	   #
	   # Check if you have distutils, else fail
	   #
	   AC_MSG_CHECKING([for the sysconfig Python package])
	   ac_sysconfig_result=`$PYTHON -c "import sysconfig" 2>&1`
	   if test $? -eq 0; then
		AC_MSG_RESULT([yes])
		IMPORT_SYSCONFIG="import sysconfig"
	   else
		AC_MSG_RESULT([no])

		AC_MSG_CHECKING([for the distutils Python package])
		ac_sysconfig_result=`$PYTHON -c "from distutils import sysconfig" 2>&1`
		if test $? -eq 0; then
			AC_MSG_RESULT([yes])
			IMPORT_SYSCONFIG="from distutils import sysconfig"
		else
			AC_MSG_WARN([cannot import Python module "distutils".
Please check your Python installation. The error was:
$ac_sysconfig_result])
			if ! $ax_python_devel_optional; then
			   AC_MSG_ERROR([Giving up])
			fi
			ax_python_devel_found=no
			PYTHON_VERSION=""
		fi
	   fi
	fi

	if test $ax_python_devel_found = yes; then
	   #
	   # Check for Python include path
	   #
	   AC_MSG_CHECKING([for Python include path])
	   if test -z "$PYTHON_CPPFLAGS"; then
		if test "$IMPORT_SYSCONFIG" = "import sysconfig"; then
			# sysconfig module has different functions
			python_path=`$PYTHON -c "$IMPORT_SYSCONFIG; \
				print (sysconfig.get_path ('include'));"`
			plat_python_path=`$PYTHON -c "$IMPORT_SYSCONFIG; \
				print (sysconfig.get_path ('platinclude'));"`
		else
			# old distutils way
			python_path=`$PYTHON -c "$IMPORT_SYSCONFIG; \
				print (sysconfig.get_python_inc ());"`
			plat_python_path=`$PYTHON -c "$IMPORT_SYSCONFIG; \
				print (sysconfig.get_python_inc (plat_specific=1));"`
		fi
		if test -n "${python_path}"; then
			if test "${plat_python_path}" != "${python_path}"; then
				python_path="-I$python_path -I$plat_python_path"
			else
				python_path="-I$python_path"
			fi
		fi
		PYTHON_CPPFLAGS=$python_path
	   fi
	   AC_MSG_RESULT([$PYTHON_CPPFLAGS])
	   AC_SUBST([PYTHON_CPPFLAGS])

	   #
	   # Check for Python library path
	   #
	   AC_MSG_CHECKING([for Python library path])
	   if test -z "$PYTHON_LIBS"; then
		# (makes two attempts to ensure we've got a version number
		# from the interpreter)
		ac_python_version=`cat<<EOD | $PYTHON -

# join all versioning strings, on some systems
# major/minor numbers could be in different list elements
from sysconfig import *
e = get_config_var('VERSION')
if e is not None:
	print(e)
EOD`

		if test -z "$ac_python_version"; then
			if test -n "$PYTHON_VERSION"; then
				ac_python_version=$PYTHON_VERSION
			else
				ac_python_version=`$PYTHON -c "import sys; \
					print ("%d.%d" % sys.version_info[[:2]])"`
			fi
		fi

		# Make the versioning information available to the compiler
		AC_DEFINE_UNQUOTED([HAVE_PYTHON], ["$ac_python_version"],
                                   [If available, contains the Python version number currently in use.])

		# First, the library directory:
		ac_python_libdir=`cat<<EOD | $PYTHON -

# There should be only one
$IMPORT_SYSCONFIG
e = sysconfig.get_config_var('LIBDIR')
if e is not None:
	print (e)
EOD`

		# Now, for the library:
		ac_python_library=`cat<<EOD | $PYTHON -

$IMPORT_SYSCONFIG
c = sysconfig.get_config_vars()
if 'LDVERSION' in c:
	print ('python'+c[['LDVERSION']])
else:
	print ('python'+c[['VERSION']])
EOD`

		# This small piece shamelessly adapted from PostgreSQL python macro;
		# credits goes to momjian, I think. I'd like to put the right name
		# in the credits, if someone can point me in the right direction... ?
		#
		if test -n "$ac_python_libdir" -a -n "$ac_python_library"
		then
			# use the official shared library
			ac_python_library=`echo "$ac_python_library" | sed "s/^lib//"`
			PYTHON_LIBS="-L$ac_python_libdir -l$ac_python_library"
		else
			# old way: use libpython from python_configdir
			ac_python_libdir=`$PYTHON -c \
			  "from sysconfig import get_python_lib as f; \
			  import os; \
			  print (os.path.join(f(plat_specific=1, standard_lib=1), 'config'));"`
			PYTHON_LIBS="-L$ac_python_libdir -lpython$ac_python_version"
		fi

		if test -z "PYTHON_LIBS"; then
			AC_MSG_WARN([
  Cannot determine location of your Python DSO. Please check it was installed with
  dynamic libraries enabled, or try setting PYTHON_LIBS by hand.
			])
			if ! $ax_python_devel_optional; then
			   AC_MSG_ERROR([Giving up])
			fi
			ax_python_devel_found=no
			PYTHON_VERSION=""
		fi
	   fi
	fi

	if test $ax_python_devel_found = yes; then
	   AC_MSG_RESULT([$PYTHON_LIBS])
	   AC_SUBST([PYTHON_LIBS])

	   #
	   # Check for site packages
	   #
	   AC_MSG_CHECKING([for Python site-packages path])
	   if test -z "$PYTHON_SITE_PKG"; then
		if test "$IMPORT_SYSCONFIG" = "import sysconfig"; then
			PYTHON_SITE_PKG=`$PYTHON -c "
$IMPORT_SYSCONFIG;
if hasattr(sysconfig, 'get_default_scheme'):
    scheme = sysconfig.get_default_scheme()
else:
    scheme = sysconfig._get_default_scheme()
if scheme == 'posix_local':
    # Debian's default scheme installs to /usr/local/ but we want to find headers in /usr/
    scheme = 'posix_prefix'
prefix = '$prefix'
if prefix == 'NONE':
    prefix = '$ac_default_prefix'
sitedir = sysconfig.get_path('purelib', scheme, vars={'base': prefix})
print(sitedir)"`
		else
			# distutils.sysconfig way
			PYTHON_SITE_PKG=`$PYTHON -c "$IMPORT_SYSCONFIG; \
				print (sysconfig.get_python_lib(0,0));"`
		fi
	   fi
	   AC_MSG_RESULT([$PYTHON_SITE_PKG])
	   AC_SUBST([PYTHON_SITE_PKG])

	   #
	   # Check for platform-specific site packages
	   #
	   AC_MSG_CHECKING([for Python platform specific site-packages path])
	   if test -z "$PYTHON_PLATFORM_SITE_PKG"; then
		if test "$IMPORT_SYSCONFIG" = "import sysconfig"; then
			PYTHON_PLATFORM_SITE_PKG=`$PYTHON -c "
$IMPORT_SYSCONFIG;
if hasattr(sysconfig, 'get_default_scheme'):
    scheme = sysconfig.get_default_scheme()
else:
    scheme = sysconfig._get_default_scheme()
if scheme == 'posix_local':
    # Debian's default scheme installs to /usr/local/ but we want to find headers in /usr/
    scheme = 'posix_prefix'
prefix = '$prefix'
if prefix == 'NONE':
    prefix = '$ac_default_prefix'
sitedir = sysconfig.get_path('platlib', scheme, vars={'platbase': prefix})
print(sitedir)"`
		else
			# distutils.sysconfig way
			PYTHON_PLATFORM_SITE_PKG=`$PYTHON -c "$IMPORT_SYSCONFIG; \
				print (sysconfig.get_python_lib(1,0));"`
		fi
	   fi
	   AC_MSG_RESULT([$PYTHON_PLATFORM_SITE_PKG])
	   AC_SUBST([PYTHON_PLATFORM_SITE_PKG])

	   #
	   # libraries which must be linked in when embedding
	   #
	   AC_MSG_CHECKING(python extra libraries)
	   if test -z "$PYTHON_EXTRA_LIBS"; then
	      PYTHON_EXTRA_LIBS=`$PYTHON -c "$IMPORT_SYSCONFIG; \
                conf = sysconfig.get_config_var; \
                print (conf('LIBS') + ' ' + conf('SYSLIBS'))"`
	   fi
	   AC_MSG_RESULT([$PYTHON_EXTRA_LIBS])
	   AC_SUBST(PYTHON_EXTRA_LIBS)

	   #
	   # linking flags needed when embedding
	   #
	   AC_MSG_CHECKING(python extra linking flags)
	   if test -z "$PYTHON_EXTRA_LDFLAGS"; then
		PYTHON_EXTRA_LDFLAGS=`$PYTHON -c "$IMPORT_SYSCONFIG; \
			conf = sysconfig.get_config_var; \
			print (conf('LINKFORSHARED'))"`
		# Hack for macos, it sticks this in here.
		PYTHON_EXTRA_LDFLAGS=`echo $PYTHON_EXTRA_LDFLAGS | sed 's/CoreFoundation.*$/CoreFoundation/'`
	   fi
	   AC_MSG_RESULT([$PYTHON_EXTRA_LDFLAGS])
	   AC_SUBST(PYTHON_EXTRA_LDFLAGS)

	   #
	   # final check to see if everything compiles alright
	   #
	   AC_MSG_CHECKING([consistency of all components of python development environment])
	   # save current global flags
	   ac_save_LIBS="$LIBS"
	   ac_save_LDFLAGS="$LDFLAGS"
	   ac_save_CPPFLAGS="$CPPFLAGS"
	   LIBS="$ac_save_LIBS $PYTHON_LIBS $PYTHON_EXTRA_LIBS"
	   LDFLAGS="$ac_save_LDFLAGS $PYTHON_EXTRA_LDFLAGS"
	   CPPFLAGS="$ac_save_CPPFLAGS $PYTHON_CPPFLAGS"
	   AC_LANG_PUSH([C])
	   AC_LINK_IFELSE([
		AC_LANG_PROGRAM([[#include <Python.h>]],
				[[Py_Initialize();]])
		],[pythonexists=yes],[pythonexists=no])
	   AC_LANG_POP([C])
	   # turn back to default flags
	   CPPFLAGS="$ac_save_CPPFLAGS"
	   LIBS="$ac_save_LIBS"
	   LDFLAGS="$ac_save_LDFLAGS"

	   AC_MSG_RESULT([$pythonexists])

	   if test ! "x$pythonexists" = "xyes"; then
	      AC_MSG_WARN([
  Could not link test program to Python. Maybe the main Python library has been
  installed in some non-standard library path. If so, pass it to configure,
  via the LIBS environment variable.
  Example: ./configure LIBS="-L/usr/non-standard-path/python/lib"
  ============================================================================
   ERROR!
   You probably have to install the development version of the Python package
   for your distribution.  The exact name of this package varies among them.
  ============================================================================
	      ])
	      if ! $ax_python_devel_optional; then
		 AC_MSG_ERROR([Giving up])
	      fi
	      ax_python_devel_found=no
	      PYTHON_VERSION=""
	   fi
	fi

	#
	# all done!
	#
])
