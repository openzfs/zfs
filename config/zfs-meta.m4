###############################################################################
# Written by Chris Dunlap <cdunlap@llnl.gov>.
# Modified by Brian Behlendorf <behlendorf1@llnl.gov>.
###############################################################################
# ZFS_AC_META: Read metadata from the META file.  When building from a
# git repository the ZFS_META_RELEASE field will be overwritten if there
# is an annotated tag matching the form ZFS_META_NAME-ZFS_META_VERSION-*.
# This allows for working builds to be uniquely identified using the git
# commit hash.
###############################################################################

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

	AC_MSG_CHECKING([metadata])

	META="$srcdir/META"
	_zfs_ac_meta_type="none"
	if test -f "$META"; then
		_zfs_ac_meta_type="META file"

		ZFS_META_NAME=_ZFS_AC_META_GETVAL([(?:NAME|PROJECT|PACKAGE)]);
		if test -n "$ZFS_META_NAME"; then
			AC_DEFINE_UNQUOTED([ZFS_META_NAME], ["$ZFS_META_NAME"],
				[Define the project name.]
			)
			AC_SUBST([ZFS_META_NAME])
		fi

		ZFS_META_VERSION=_ZFS_AC_META_GETVAL([VERSION]);
		if test -n "$ZFS_META_VERSION"; then
			AC_DEFINE_UNQUOTED([ZFS_META_VERSION], ["$ZFS_META_VERSION"],
				[Define the project version.]
			)
			AC_SUBST([ZFS_META_VERSION])
		fi

		ZFS_META_RELEASE=_ZFS_AC_META_GETVAL([RELEASE]);
		if git rev-parse --git-dir > /dev/null 2>&1; then
			_match="${ZFS_META_NAME}-${ZFS_META_VERSION}*"
			_alias=$(git describe --match=${_match} 2>/dev/null)
			_release=$(echo ${_alias}|cut -f3- -d'-'|sed 's/-/_/g')
			if test -n "${_release}"; then
				ZFS_META_RELEASE=${_release}
				_zfs_ac_meta_type="git describe"
			fi
		fi

		if test -n "$ZFS_META_RELEASE"; then
			AC_DEFINE_UNQUOTED([ZFS_META_RELEASE], ["$ZFS_META_RELEASE"],
				[Define the project release.]
			)
			AC_SUBST([ZFS_META_RELEASE])

			RELEASE="$ZFS_META_RELEASE"
			AC_SUBST([RELEASE])
		fi

		ZFS_META_LICENSE=_ZFS_AC_META_GETVAL([LICENSE]);
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
					[Define the project alias string.] 
				)
				AC_SUBST([ZFS_META_ALIAS])
		fi

		ZFS_META_DATA=_ZFS_AC_META_GETVAL([DATE]);
		if test -n "$ZFS_META_DATA"; then
			AC_DEFINE_UNQUOTED([ZFS_META_DATA], ["$ZFS_META_DATA"],
				[Define the project release date.] 
			)
			AC_SUBST([ZFS_META_DATA])
		fi

		ZFS_META_AUTHOR=_ZFS_AC_META_GETVAL([AUTHOR]);
		if test -n "$ZFS_META_AUTHOR"; then
			AC_DEFINE_UNQUOTED([ZFS_META_AUTHOR], ["$ZFS_META_AUTHOR"],
				[Define the project author.]
			)
			AC_SUBST([ZFS_META_AUTHOR])
		fi

		m4_pattern_allow([^LT_(CURRENT|REVISION|AGE)$])
		ZFS_META_LT_CURRENT=_ZFS_AC_META_GETVAL([LT_CURRENT]);
		ZFS_META_LT_REVISION=_ZFS_AC_META_GETVAL([LT_REVISION]);
		ZFS_META_LT_AGE=_ZFS_AC_META_GETVAL([LT_AGE]);
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

AC_DEFUN([_ZFS_AC_META_GETVAL], 
	[`perl -n\
		-e "BEGIN { \\$key=shift @ARGV; }"\
		-e "next unless s/^\s*\\$key@<:@:=@:>@//i;"\
		-e "s/^((?:@<:@^'\"#@:>@*(?:(@<:@'\"@:>@)@<:@^\2@:>@*\2)*)*)#.*/\\@S|@1/;"\
		-e "s/^\s+//;"\
		-e "s/\s+$//;"\
		-e "s/^(@<:@'\"@:>@)(.*)\1/\\@S|@2/;"\
		-e "\\$val=\\$_;"\
		-e "END { print \\$val if defined \\$val; }"\
		'$1' $META`]dnl
)
