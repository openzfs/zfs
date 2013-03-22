###############################################################################
# Written by Chris Dunlap <cdunlap@llnl.gov>.
# Modified by Brian Behlendorf <behlendorf1@llnl.gov>.
###############################################################################
# SPL_AC_META: Read metadata from the META file.  When building from a
# git repository the SPL_META_RELEASE field will be overwritten if there
# is an annotated tag matching the form SPL_META_NAME-SPL_META_VERSION-*.
# This allows for working builds to be uniquely identified using the git
# commit hash.
###############################################################################

AC_DEFUN([SPL_AC_META], [
	AC_MSG_CHECKING([metadata])

	META="$srcdir/META"
	_spl_ac_meta_type="none"
	if test -f "$META"; then
		_spl_ac_meta_type="META file"

		SPL_META_NAME=_SPL_AC_META_GETVAL([(?:NAME|PROJECT|PACKAGE)]);
		if test -n "$SPL_META_NAME"; then
			AC_DEFINE_UNQUOTED([SPL_META_NAME], ["$SPL_META_NAME"],
				[Define the project name.]
			)
			AC_SUBST([SPL_META_NAME])
		fi

		SPL_META_VERSION=_SPL_AC_META_GETVAL([VERSION]);
		if test -n "$SPL_META_VERSION"; then
			AC_DEFINE_UNQUOTED([SPL_META_VERSION], ["$SPL_META_VERSION"],
				[Define the project version.]
			)
			AC_SUBST([SPL_META_VERSION])
		fi

		SPL_META_RELEASE=_SPL_AC_META_GETVAL([RELEASE]);
		if git rev-parse --git-dir > /dev/null 2>&1; then
			_match="${SPL_META_NAME}-${SPL_META_VERSION}*"
			_alias=$(git describe --match=${_match} 2>/dev/null)
			_release=$(echo ${_alias}|cut -f3- -d'-'|sed 's/-/_/g')
			if test -n "${_release}"; then
				SPL_META_RELEASE=${_release}
				_spl_ac_meta_type="git describe"
			fi
		fi

		if test -n "$SPL_META_RELEASE"; then
			AC_DEFINE_UNQUOTED([SPL_META_RELEASE], ["$SPL_META_RELEASE"],
				[Define the project release.]
			)
			AC_SUBST([SPL_META_RELEASE])

			RELEASE="$SPL_META_RELEASE"
			AC_SUBST([RELEASE])
		fi

		if test -n "$SPL_META_NAME" -a -n "$SPL_META_VERSION"; then
				SPL_META_ALIAS="$SPL_META_NAME-$SPL_META_VERSION"
				test -n "$SPL_META_RELEASE" && 
				        SPL_META_ALIAS="$SPL_META_ALIAS-$SPL_META_RELEASE"
				AC_DEFINE_UNQUOTED([SPL_META_ALIAS],
					["$SPL_META_ALIAS"],
					[Define the project alias string.] 
				)
				AC_SUBST([SPL_META_ALIAS])
		fi

		SPL_META_DATA=_SPL_AC_META_GETVAL([DATE]);
		if test -n "$SPL_META_DATA"; then
			AC_DEFINE_UNQUOTED([SPL_META_DATA], ["$SPL_META_DATA"],
				[Define the project release date.] 
			)
			AC_SUBST([SPL_META_DATA])
		fi

		SPL_META_AUTHOR=_SPL_AC_META_GETVAL([AUTHOR]);
		if test -n "$SPL_META_AUTHOR"; then
			AC_DEFINE_UNQUOTED([SPL_META_AUTHOR], ["$SPL_META_AUTHOR"],
				[Define the project author.]
			)
			AC_SUBST([SPL_META_AUTHOR])
		fi

		m4_pattern_allow([^LT_(CURRENT|REVISION|AGE)$])
		SPL_META_LT_CURRENT=_SPL_AC_META_GETVAL([LT_CURRENT]);
		SPL_META_LT_REVISION=_SPL_AC_META_GETVAL([LT_REVISION]);
		SPL_META_LT_AGE=_SPL_AC_META_GETVAL([LT_AGE]);
		if test -n "$SPL_META_LT_CURRENT" \
				 -o -n "$SPL_META_LT_REVISION" \
				 -o -n "$SPL_META_LT_AGE"; then
			test -n "$SPL_META_LT_CURRENT" || SPL_META_LT_CURRENT="0"
			test -n "$SPL_META_LT_REVISION" || SPL_META_LT_REVISION="0"
			test -n "$SPL_META_LT_AGE" || SPL_META_LT_AGE="0"
			AC_DEFINE_UNQUOTED([SPL_META_LT_CURRENT],
				["$SPL_META_LT_CURRENT"],
				[Define the libtool library 'current'
				 version information.]
			)
			AC_DEFINE_UNQUOTED([SPL_META_LT_REVISION],
				["$SPL_META_LT_REVISION"],
				[Define the libtool library 'revision'
				 version information.]
			)
			AC_DEFINE_UNQUOTED([SPL_META_LT_AGE], ["$SPL_META_LT_AGE"],
				[Define the libtool library 'age' 
				 version information.]
			)
			AC_SUBST([SPL_META_LT_CURRENT])
			AC_SUBST([SPL_META_LT_REVISION])
			AC_SUBST([SPL_META_LT_AGE])
		fi
	fi

	AC_MSG_RESULT([$_spl_ac_meta_type])
	]
)

AC_DEFUN([_SPL_AC_META_GETVAL], 
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
