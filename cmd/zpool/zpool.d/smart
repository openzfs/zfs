#!/bin/sh
#
# Show SMART stats
#

helpstr="
smart:		Show SMART temperature and error stats (specific to drive type)
smartx:		Show SMART extended drive stats (specific to drive type).
temp:		Show SMART drive temperature in celsius (all drives).
health:		Show reported SMART status (all drives).
r_proc:		Show SMART read GBytes processed over drive lifetime (SAS).
w_proc:		Show SMART write GBytes processed over drive lifetime (SAS).
r_ucor:		Show SMART read uncorrectable errors (SAS).
w_ucor:		Show SMART write uncorrectable errors (SAS).
nonmed:		Show SMART non-medium errors (SAS).
defect:		Show SMART grown defect list (SAS).
hours_on:	Show number of hours drive powered on (all drives).
realloc:	Show SMART reallocated sectors count (ATA).
rep_ucor:	Show SMART reported uncorrectable count (ATA).
cmd_to:		Show SMART command timeout count (ATA).
pend_sec:	Show SMART current pending sector count (ATA).
off_ucor:	Show SMART offline uncorrectable errors (ATA).
ata_err:	Show SMART ATA errors (ATA).
pwr_cyc:	Show SMART power cycle count (ATA).
serial:		Show disk serial number.
nvme_err:	Show SMART NVMe errors (NVMe).
smart_test:	Show SMART self-test results summary.
test_type:	Show SMART self-test type (short, long... ).
test_status:	Show SMART self-test status.
test_progress:	Show SMART self-test percentage done.
test_ended:	Show when the last SMART self-test ended (if supported).
"

# Hack for developer testing
#
# If you set $samples to a directory containing smartctl output text files,
# we will use them instead of running smartctl on the vdevs.  This can be
# useful if you want to test a bunch of different smartctl outputs.  Also, if
# $samples is set, and additional 'file' column is added to the zpool output
# showing the filename.
samples=

# get_filename_from_dir DIR
#
# Look in directory DIR and return a filename from it.  The filename returned
# is chosen quasi-sequentially (based off our PID).  This allows us to return
# a different filename every time this script is invoked (which we do for each
# vdev), without having to maintain state.
get_filename_from_dir()
{
	dir=$1
	pid="$$"
	num_files=$(find "$dir" -maxdepth 1 -type f | wc -l)
	mod=$((pid % num_files))
	i=0
	find "$dir" -type f -printf '%f\n' | while read -r file ; do
		if [ "$mod" = "$i" ] ; then
			echo "$file"
			break
		fi
		i=$((i+1))
	done
}

script="${0##*/}"

if [ "$1" = "-h" ] ; then
        echo "$helpstr" | grep "$script:" | tr -s '\t' | cut -f 2-
        exit
fi

# Sometimes, UPATH ends up /dev/(null).
# That should be corrected, but for now...
# shellcheck disable=SC2154
if [ ! -b "$VDEV_UPATH" ]; then
	somepath="${VDEV_PATH}"
else
	somepath="${VDEV_UPATH}"
fi

if [ -b "$somepath" ] && PATH="/usr/sbin:$PATH" command -v smartctl > /dev/null || [ -n "$samples" ] ; then
	if [ -n "$samples" ] ; then
		# cat a smartctl output text file instead of running smartctl
		# on a vdev (only used for developer testing).
		file=$(get_filename_from_dir "$samples")
		echo "file=$file"
		raw_out=$(cat "$samples/$file")
	else
		raw_out=$(sudo smartctl -a "$somepath")
	fi

	# What kind of drive are we?  Look for the right line in smartctl:
	#
	# SAS:
	#	Transport protocol:   SAS
	#
	# SATA:
	#	ATA Version is:   8
	#
	# NVMe:
	#       SMART/Health Information (NVMe Log 0xnn, NSID 0xnn)
	#
	out=$(echo "$raw_out" | awk '
# SAS specific
/read:/{print "rrd="$4"\nr_cor="$5"\nr_proc="$7"\nr_ucor="$8}
/write:/{print "rwr="$4"\nw_cor="$5"\nw_proc="$7"\nw_ucor="$8}
/Non-medium error count/{print "nonmed="$4}
/Elements in grown defect list/{print "defect="$6}

# SAS common
/SAS/{type="sas"}
/Drive Temperature:/{print "temp="$4}
# Status can be a long string, substitute spaces for '_'
/SMART Health Status:/{printf "health="; for(i=4;i<=NF-1;i++){printf "%s_", $i}; printf "%s\n", $i}
/number of hours powered up/{print "hours_on="$7; hours_on=int($7)}
/Serial number:/{print "serial="$3}

# SATA specific
/Reallocated_Sector_Ct/{print "realloc="$10}
/Reported_Uncorrect/{print "rep_ucor="$10}
/Command_Timeout/{print "cmd_to="$10}
/Current_Pending_Sector/{print "pend_sec="$10}
/Offline_Uncorrectable/{print "off_ucor="$10}
/ATA Error Count:/{print "ata_err="$4}
/Power_Cycle_Count/{print "pwr_cyc="$10}

# SATA common
/SATA/{type="sata"}
/Temperature_Celsius/{print "temp="$10}
/Airflow_Temperature_Cel/{print "temp="$10}
/Current Temperature:/{print "temp="$3}
/SMART overall-health self-assessment test result:/{print "health="$6}
/Power_On_Hours/{print "hours_on="$10; hours_on=int($10)}
/Serial Number:/{print "serial="$3}

# NVMe common
/NVMe/{type="nvme"}
/Temperature:/{print "temp="$2}
/SMART overall-health self-assessment test result:/{print "health="$6}
/Power On Hours:/{gsub("[^0-9]","",$4); print "hours_on="$4}
/Serial Number:/{print "serial="$3}
/Power Cycles:/{print "pwr_cyc="$3}

# NVMe specific
/Media and Data Integrity Errors:/{print "nvme_err="$6}

# SMART self-test info
/Self-test execution status:/{progress=tolower($4)} # SAS
/SMART Self-test log/{test_seen=1} # SAS
/SMART Extended Self-test Log/{test_seen=1} # SATA
/# 1/{
	test_type=tolower($3"_"$4);
	# Status could be one word ("Completed") or multiple ("Completed: read
	# failure").  Look for the ":" to see if we need to grab more words.

	if ($5 ~ ":")
		status=tolower($5""$6"_"$7)
	else
		status=tolower($5)
	if (status=="self")
		status="running";

	if (type == "sas") {
		hours=int($(NF-4))
	} else {
		hours=int($(NF-1))
		# SATA reports percent remaining, rather than percent done
		# Convert it to percent done.
		progress=(100-int($(NF-2)))"%"
	}
	# When we int()-ify "hours", it converts stuff like "NOW" and "-" into
	# 0.  In those cases, set it to hours_on, so they will cancel out in
	# the "hours_ago" calculation later on.
	if (hours == 0)
		hours=hours_on

	if (test_seen) {
		print "test="hours_on
		print "test_type="test_type
		print "test_status="status
		print "test_progress="progress
	}
	# Not all drives report hours_on
	if (hours_on && hours) {
		total_hours_ago=(hours_on-hours)
		days_ago=int(total_hours_ago/24)
		hours_ago=(total_hours_ago % 24)
		if (days_ago != 0)
			ago_str=days_ago"d"
		if (hours_ago !=0)
			ago_str=ago_str""hours_ago"h"
		print "test_ended="ago_str
	}
}

END {print "type="type; ORS="\n"; print ""}
');
fi
type=$(echo "$out" | grep '^type=' | cut -d '=' -f 2)

# If type is not set by now, either we don't have a block device
# or smartctl failed. Either way, default to ATA and set $out to
# nothing.
if [ -z "$type" ]; then
	type="sata"
	out=
fi

case $script in
smart)
	# Print temperature plus common predictors of drive failure
	if [ "$type" = "sas" ] ; then
		scripts="temp|health|r_ucor|w_ucor"
	elif [ "$type" = "sata" ] ; then
		scripts="temp|health|ata_err|realloc|rep_ucor|cmd_to|pend_sec|off_ucor"
	elif [ "$type" = "nvme" ] ; then
		scripts="temp|health|nvme_err"
	fi
	;;
smartx)
	# Print some other interesting stats
	if [ "$type" = "sas" ] ; then
		scripts="hours_on|defect|nonmed|r_proc|w_proc"
	elif [ "$type" = "sata" ] ; then
		scripts="hours_on|pwr_cyc"
	elif [ "$type" = "nvme" ] ; then
		scripts="hours_on|pwr_cyc"
	fi
	;;
smart_test)
	scripts="test_type|test_status|test_progress|test_ended"
	;;
*)
	scripts="$script"
esac

with_vals=$(echo "$out" | grep -E "$scripts")
if [ -n "$with_vals" ]; then
	echo "$with_vals"
	without_vals=$(echo "$scripts" | tr '|' '\n' |
		grep -v -E "$(echo "$with_vals" |
		awk -F "=" '{print $1}')" | awk '{print $0"="}')
else
	without_vals=$(echo "$scripts" | tr '|' '\n' | awk '{print $0"="}')
fi

if [ -n "$without_vals" ]; then
	echo "$without_vals"
fi
