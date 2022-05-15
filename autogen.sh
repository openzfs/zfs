#!/bin/sh
# shellcheck disable=SC2016,SC2094
[ "${0%/*}" = "$0" ] || cd "${0%/*}" || exit

# %reldir%/%canon_reldir% (%D%/%C%) only appeared in automake 1.14, but RHEL/CentOS 7 has 1.13.4
# This is an (overly) simplistic preprocessor that papers around this for the duration of the generation step,
# and can be removed once support for CentOS 7 is dropped
automake --version | awk '{print $NF; exit}' | (
	IFS=. read -r AM_MAJ AM_MIN _
	[ "$AM_MAJ" -gt 1 ] || [ "$AM_MIN" -ge 14 ]
) || {
	process_root() {
		root="$1"; shift

		grep -q '%[CD]%' "$root/Makefile.am" || return
		find "$root" -name Makefile.am "$@" | while read -r dir; do
			dir="${dir%/Makefile.am}"
			grep -q '%[CD]%' "$dir/Makefile.am" || continue

			reldir="${dir#$root}"
			reldir="${reldir#/}"

			canon_reldir="$(printf '%s' "$reldir" | tr -C 'a-zA-Z0-9@_' '_')"

			reldir_slash="$reldir/"
			canon_reldir_slash="${canon_reldir}_"
			[ -z "$reldir" ] && reldir_slash=
			[ -z "$reldir" ] && canon_reldir_slash=

			echo "$dir/Makefile.am" >&3
			sed -i~ -e "s:%D%/:$reldir_slash:g"       -e "s:%D%:$reldir:g" \
			        -e "s:%C%_:$canon_reldir_slash:g" -e "s:%C%:$canon_reldir:g" "$dir/Makefile.am"
		done 3>>"$substituted_files"
	}

	rollback() {
		while read -r f; do
			mv "$f~" "$f"
		done < "$substituted_files"
		rm -f "$substituted_files"
	}


	echo "Automake <1.14; papering over missing %reldir%/%canon_reldir% support" >&2

	substituted_files="$(mktemp)"
	trap rollback EXIT

	roots="$(sed '/Makefile$/!d;/module/d;s:^\s*:./:;s:/Makefile::;/^\.$/d' configure.ac)"

	OIFS="$IFS"
	IFS="
"
	for root in $roots; do
		root="${root#./}"
		process_root "$root"
	done

	set -f
	# shellcheck disable=SC2086,SC2046
	process_root . $(printf '!\n-path\n%s/*\n' $roots)
	IFS="$OIFS"
}

autoreconf -fiv && rm -rf autom4te.cache

# https://github.com/openzfs/zfs/issues/13459
# https://bugs.debian.org/1011024
# When including, EXTRA_DIST in false conditionals is skipped; we need to preprocess Makefile.in to unbreak this.
# The same happens to dist_man_MANS (DISTFILES -> DIST_COMMON -> am__DIST_COMMON -> dist_man_MANS).
#
# Shortcuts taken:
#   * only the root Makefile.in is processed because it's the only one that does if, include; it takes too long to process this as-is (~0.3s)
#   * only one level of :suff=suff allowed
{
	deverbol() {
		type rollback > /dev/null 2>&1 && rollback
		rm -f "$verbols"
	}

	trap deverbol EXIT
	verbols="$(mktemp)"

	parse_verbols() {
		v="${v#\$\(}"
		v="${v%\)}"
		[ -z "$suff_bundle" ] && {
			suff_bundle="${v#*:}"
			[ "$suff_bundle" = "$v" ] && suff_bundle= || suff_bundle=":$suff_bundle"
		}
		v="${v%:*}"
	}

	printf "%s\n" dist_man_MANS EXTRA_DIST > "$verbols"
	while read -r v suff_bundle; do
		parse_verbols
		# Early exit buys back 0.43s here
		awk -v v="$v" '($0 ~ ("^(@[A-Z_@]*@)?" v " =")),!/\\$/ {copying=1; print}  copying && !/\\$/ {exit}' Makefile.in |
			grep -o '$([^ )]*)' |
			sed 's/$/ '"$suff_bundle"'/'
	done < "$verbols" >> "$verbols"
	sort "$verbols" | uniq |
	while read -r v suff_bundle; do
		parse_verbols
		# And another 0.34s here
		awk -v v="$v" '
				BEGIN {
					printf "faux_" v "_faux := "
				}
				($0 ~ ("^@[A-Z_@]*@" v " =")),!/\\$/ {
					l = $0
					sub(/^@[A-Z_@]*@/, "", l)
					sub("^" v " =", "", l)
					sub(/\\$/, "", l)
					printf "%s", l
					copying = 1
				}
				copying && !/\\$/ {
					exit
				}
			' Makefile.in
		echo
		echo "EXTRA_DIST += \$(faux_${v}_faux${suff_bundle})"
	done >> Makefile.in
}
