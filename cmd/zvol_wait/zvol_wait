#!/bin/sh

count_zvols() {
	if [ -z "$zvols" ]; then
		echo 0
	else
		echo "$zvols" | wc -l
	fi
}

filter_out_zvols_with_links() {
	echo "$zvols" | tr ' ' '+' | while read -r zvol; do
		if ! [ -L "/dev/zvol/$zvol" ]; then
			echo "$zvol"
		fi
	done | tr '+' ' '
}

filter_out_deleted_zvols() {
	OIFS="$IFS"
	IFS="
"
	# shellcheck disable=SC2086
	zfs list -H -o name $zvols 2>/dev/null
	IFS="$OIFS"
}

list_zvols() {
	read -r default_volmode < /sys/module/zfs/parameters/zvol_volmode
	zfs list -t volume -H -o \
	    name,volmode,receive_resume_token,redact_snaps,keystatus |
	    while IFS="	" read -r name volmode token redacted keystatus; do # IFS=\t here!

		# /dev links are not created for zvols with volmode = "none",
		# redacted zvols, or encrypted zvols for which the key has not
		# been loaded.
		[ "$volmode" = "none" ] && continue
		[ "$volmode" = "default" ] && [ "$default_volmode" = "3" ] &&
		    continue
		[ "$redacted" = "-" ] || continue
		[ "$keystatus" = "unavailable" ] && continue

		# We also ignore partially received zvols if it is
		# not an incremental receive, as those won't even have a block
		# device minor node created yet.
		if [ "$token" != "-" ]; then

			# Incremental receives create an invisible clone that
			# is not automatically displayed by zfs list.
			if ! zfs list "$name/%recv" >/dev/null 2>&1; then
				continue
			fi
		fi
		echo "$name"
	done
}

zvols=$(list_zvols)
zvols_count=$(count_zvols)
if [ "$zvols_count" -eq 0 ]; then
	echo "No zvols found, nothing to do."
	exit 0
fi

echo "Testing $zvols_count zvol links"

outer_loop=0
while [ "$outer_loop" -lt 20 ]; do
	outer_loop=$((outer_loop + 1))

	old_zvols_count=$(count_zvols)

	inner_loop=0
	while [ "$inner_loop" -lt 30 ]; do
		inner_loop=$((inner_loop + 1))

		zvols="$(filter_out_zvols_with_links)"

		zvols_count=$(count_zvols)
		if [ "$zvols_count" -eq 0 ]; then
			echo "All zvol links are now present."
			exit 0
		fi
		sleep 1
	done

	echo "Still waiting on $zvols_count zvol links ..."
	#
	# Although zvols should normally not be deleted at boot time,
	# if that is the case then their links will be missing and
	# we would stall.
	#
	if [ "$old_zvols_count" -eq "$zvols_count" ]; then
		echo "No progress since last loop."
		echo "Checking if any zvols were deleted."

		zvols=$(filter_out_deleted_zvols)
		zvols_count=$(count_zvols)

		if [ "$old_zvols_count" -ne "$zvols_count" ]; then
			echo "$((old_zvols_count - zvols_count)) zvol(s) deleted."
		fi

		if [ "$zvols_count" -ne 0 ]; then
			echo "Remaining zvols:"
			echo "$zvols"
		else
			echo "All zvol links are now present."
			exit 0
		fi
	fi

	#
	# zvol_count made some progress - let's stay in this loop.
	#
	if [ "$old_zvols_count" -gt "$zvols_count" ]; then
		outer_loop=$((outer_loop - 1))
	fi
done

echo "Timed out waiting on zvol links"
exit 1
