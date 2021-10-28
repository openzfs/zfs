dnl #
dnl # DESCRIPTION:
dnl # Read meta data from the META file.  When building from a git repository
dnl # the ZFS_META_RELEASE field will be overwritten if there is an annotated
dnl # tag matching the form ZFS_META_NAME-ZFS_META_VERSION-*.  This allows
dnl # for working builds to be uniquely identified using the git commit hash.
dnl #
dnl #    The META file format is as follows:
dnl #      ^[ ]*KEY:[ \t]+VALUE$
dnl #
dnl #    In other words:
dnl #    - KEY is separated from VALUE by a colon and one or more spaces/tabs.
dnl #    - KEY and VALUE are case sensitive.
dnl #    - Leading spaces are ignored.
dnl #    - First match wins for duplicate keys.
dnl #
dnl #    A line can be commented out by preceding it with a '#' (or technically
dnl #    any non-space character since that will prevent the regex from
dnl #    matching).
dnl #
dnl # WARNING:
dnl #   Placing a colon followed by a space or tab (ie, ":[ \t]+") within the
dnl #   VALUE will prematurely terminate the string since that sequence is
dnl #   used as the awk field separator.
dnl #
dnl # KEYS:
dnl #   The following META keys are recognized:
dnl #     Name, Version, Release, Date, Author, LT_Current, LT_Revision, LT_Age
dnl #
dnl # Written by Chris Dunlap <cdunlap@llnl.gov>.
dnl # Modified by Brian Behlendorf <behlendorf1@llnl.gov>.
dnl #
AC_DEFUN([ZFS_AC_META], [

	AH_BOTTOM([
#undef PACKAGE
#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#undef STDC_HEADERS
#undef VERSION])

	AC_PROG_AWK
	AC_MSG_CHECKING([metadata])

	META="$srcdir/META"
	_zfs_ac_meta_type="none"
	if test -f "$META"; then
		_zfs_ac_meta_type="META file"

		ZFS_META_NAME=_ZFS_AC_META_GETVAL([(Name|Project|Package)]);
		if test -n "$ZFS_META_NAME"; then
			AC_DEFINE_UNQUOTED([ZFS_META_NAME], ["$ZFS_META_NAME"],
				[Define the project name.]
			)
			AC_SUBST([ZFS_META_NAME])
		fi

		ZFS_META_VERSION=_ZFS_AC_META_GETVAL([Version]);
		if test -n "$ZFS_META_VERSION"; then
			AC_DEFINE_UNQUOTED([ZFS_META_VERSION],
			    ["$ZFS_META_VERSION"],
			    [Define the project version.])
			AC_DEFINE_UNQUOTED([SPL_META_VERSION],
			    [ZFS_META_VERSION],
			    [Defined for legacy compatibility.])
			AC_SUBST([ZFS_META_VERSION])
		fi

		ZFS_META_RELEASE=_ZFS_AC_META_GETVAL([Release]);
		if test ! -f ".nogitrelease" && git rev-parse --git-dir > /dev/null 2>&1; then
			_match="${ZFS_META_NAME}-${ZFS_META_VERSION}"
			_alias=$(git describe --match=${_match} 2>/dev/null)
			_release=$(echo ${_alias}|sed "s/${ZFS_META_NAME}//"|cut -f3- -d'-'|tr - _)
			if test -n "${_release}"; then
				ZFS_META_RELEASE=${_release}
				_zfs_ac_meta_type="git describe"
			else
				_match="${ZFS_META_NAME}-${ZFS_META_VERSION}-${ZFS_META_RELEASE}"
	                        _alias=$(git describe --match=${_match} 2>/dev/null)
				_release=$(echo ${_alias}|sed 's/${ZFS_META_NAME}//'|cut -f3- -d'-'|tr - _)
				if test -n "${_release}"; then
					ZFS_META_RELEASE=${_release}
					_zfs_ac_meta_type="git describe"
				fi
			fi
		fi

		if test -n "$ZFS_META_RELEASE"; then
			AC_DEFINE_UNQUOTED([ZFS_META_RELEASE],
			     ["$ZFS_META_RELEASE"],
			     [Define the project release.])
			AC_DEFINE_UNQUOTED([SPL_META_RELEASE],
			    [ZFS_META_RELEASE],
			    [Defined for legacy compatibility.])
			AC_SUBST([ZFS_META_RELEASE])

			RELEASE="$ZFS_META_RELEASE"
			AC_SUBST([RELEASE])
		fi

		ZFS_META_LICENSE=_ZFS_AC_META_GETVAL([License]);
		if test -n "$ZFS_META_LICENSE"; then
			AC_DEFINE_UNQUOTED([ZFS_META_LICENSE], ["$ZFS_META_LICENSE"],
				[Define the project license.]
			)
			AC_SUBST([ZFS_META_LICENSE])
		fi

		if test -n "$ZFS_META_NAME" -a -n "$ZFS_META_VERSION"; then
				ZFS_META_ALIAS="$ZFS_META_NAME-$ZFS_META_VERSION"
				test -n "$ZFS_META_RELEASE" && 
				        ZFS_META_ALIAS="$ZFS_META_ALIAS-$ZFS_META_RELEASE"
				AC_DEFINE_UNQUOTED([ZFS_META_ALIAS],
				    ["$ZFS_META_ALIAS"],
				    [Define the project alias string.])
				AC_DEFINE_UNQUOTED([SPL_META_ALIAS],
				    [ZFS_META_ALIAS],
				    [Defined for legacy compatibility.])
				AC_SUBST([ZFS_META_ALIAS])
		fi

		ZFS_META_DATA=_ZFS_AC_META_GETVAL([Date]);
		if test -n "$ZFS_META_DATA"; then
			AC_DEFINE_UNQUOTED([ZFS_META_DATA], ["$ZFS_META_DATA"],
				[Define the project release date.] 
			)
			AC_SUBST([ZFS_META_DATA])
		fi

		ZFS_META_AUTHOR=_ZFS_AC_META_GETVAL([Author]);
		if test -n "$ZFS_META_AUTHOR"; then
			AC_DEFINE_UNQUOTED([ZFS_META_AUTHOR], ["$ZFS_META_AUTHOR"],
				[Define the project author.]
			)
			AC_SUBST([ZFS_META_AUTHOR])
		fi

		ZFS_META_KVER_MIN=_ZFS_AC_META_GETVAL([Linux-Minimum]);
		if test -n "$ZFS_META_KVER_MIN"; then
			AC_DEFINE_UNQUOTED([ZFS_META_KVER_MIN],
			    ["$ZFS_META_KVER_MIN"],
			    [Define the minimum compatible kernel version.]
			)
			AC_SUBST([ZFS_META_KVER_MIN])
		fi

		ZFS_META_KVER_MAX=_ZFS_AC_META_GETVAL([Linux-Maximum]);
		if test -n "$ZFS_META_KVER_MAX"; then
			AC_DEFINE_UNQUOTED([ZFS_META_KVER_MAX],
			    ["$ZFS_META_KVER_MAX"],
			    [Define the maximum compatible kernel version.]
			)
			AC_SUBST([ZFS_META_KVER_MAX])
		fi

		m4_pattern_allow([^LT_(CURRENT|REVISION|AGE)$])
		ZFS_META_LT_CURRENT=_ZFS_AC_META_GETVAL([LT_Current]);
		ZFS_META_LT_REVISION=_ZFS_AC_META_GETVAL([LT_Revision]);
		ZFS_META_LT_AGE=_ZFS_AC_META_GETVAL([LT_Age]);
		if test -n "$ZFS_META_LT_CURRENT" \
				 -o -n "$ZFS_META_LT_REVISION" \
				 -o -n "$ZFS_META_LT_AGE"; then
			test -n "$ZFS_META_LT_CURRENT" || ZFS_META_LT_CURRENT="0"
			test -n "$ZFS_META_LT_REVISION" || ZFS_META_LT_REVISION="0"
			test -n "$ZFS_META_LT_AGE" || ZFS_META_LT_AGE="0"
			AC_DEFINE_UNQUOTED([ZFS_META_LT_CURRENT],
				["$ZFS_META_LT_CURRENT"],
				[Define the libtool library 'current'
				 version information.]
			)
			AC_DEFINE_UNQUOTED([ZFS_META_LT_REVISION],
				["$ZFS_META_LT_REVISION"],
				[Define the libtool library 'revision'
				 version information.]
			)
			AC_DEFINE_UNQUOTED([ZFS_META_LT_AGE], ["$ZFS_META_LT_AGE"],
				[Define the libtool library 'age' 
				 version information.]
			)
			AC_SUBST([ZFS_META_LT_CURRENT])
			AC_SUBST([ZFS_META_LT_REVISION])
			AC_SUBST([ZFS_META_LT_AGE])
		fi
	fi

	AC_MSG_RESULT([$_zfs_ac_meta_type])
	]
)

dnl # _ZFS_AC_META_GETVAL (KEY_NAME_OR_REGEX)
dnl #
dnl # Returns the META VALUE associated with the given KEY_NAME_OR_REGEX expr.
dnl #
dnl # Despite their resemblance to line noise,
dnl #   the "@<:@" and "@:>@" constructs are quadrigraphs for "[" and "]".
dnl #   <www.gnu.org/software/autoconf/manual/autoconf.html#Quadrigraphs>
dnl #
dnl # The "$[]1" and "$[]2" constructs prevent M4 parameter expansion
dnl #   so a literal $1 and $2 will be passed to the resulting awk script,
dnl #   whereas the "$1" will undergo M4 parameter expansion for the META key.
dnl #
AC_DEFUN([_ZFS_AC_META_GETVAL],
	[`$AWK -F ':@<:@ \t@:>@+' '$[]1 ~ /^ *$1$/ { print $[]2; exit }' $META`]dnl
)
