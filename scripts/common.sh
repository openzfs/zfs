#!/bin/bash
#
# A simple script to simply the loading/unloading the ZFS module
# stack.  It should probably be considered a first step towards
# a full ZFS init script when that is needed.
#

. ../.script-config
PROG="<define PROG>"

VERBOSE=
DUMP_LOG=
ERROR=
MODULES=()

die() {
	echo -e "${PROG}: $1" >&2
	exit 1
}

msg() {
	if [ ${VERBOSE} ]; then
		echo "$@"
	fi
}

spl_dump_log() {
        sysctl -w kernel.spl.debug.dump=1 &>/dev/null
	local NAME=`dmesg | tail -n 1 | cut -f5 -d' '`
        ${SPLBUILD}/cmd/spl ${NAME} >${NAME}.log
	echo
        echo "Dumped debug log: ${NAME}.log"
        tail -n1 ${NAME}.log
	echo
	return 0
}

check_modules() {
	local LOADED_MODULES=()
	local MISSING_MODULES=()

	for MOD in ${MODULES[*]}; do
		local NAME=`basename $MOD .ko`

		if /sbin/lsmod | egrep -q "^${NAME}"; then
			LOADED_MODULES=(${NAME} ${LOADED_MODULES[*]})
		fi

		if [ ! -f ${MOD} ]; then
			MISSING_MODULES=("\t${MOD}\n" ${MISSING_MODULES[*]})
		fi
	done

	if [ ${#LOADED_MODULES[*]} -gt 0 ]; then
		ERROR="Unload these modules with '${PROG} -u':\n"
		ERROR="${ERROR}${LOADED_MODULES[*]}"
		return 1
	fi

	if [ ${#MISSING_MODULES[*]} -gt 0 ]; then
		ERROR="The following modules can not be found,"
		ERROR="${ERROR} ensure your source trees are built:\n"
		ERROR="${ERROR}${MISSING_MODULES[*]}"
		return 1
	fi

	return 0
}

load_module() {
	local NAME=`basename $1 .ko`

	if [ ${VERBOSE} ]; then
		echo "Loading $NAME ($@)"
	fi

	/sbin/insmod $* || ERROR="Failed to load $1" return 1

	return 0
}

load_modules() {

	for MOD in ${MODULES[*]}; do
		local NAME=`basename ${MOD} .ko`
		local VALUE=

		for OPT in "$@"; do
			OPT_NAME=`echo ${OPT} | cut -f1 -d'='`
			
			if [ ${NAME} = "${OPT_NAME}" ]; then
				VALUE=`echo ${OPT} | cut -f2- -d'='`
			fi
		done

		load_module ${MOD} ${VALUE} || return 1
	done

	if [ ${VERBOSE} ]; then
		echo "Successfully loaded ZFS module stack"
	fi

	return 0
}

unload_module() {
	local NAME=`basename $1 .ko`

	if [ ${VERBOSE} ]; then
		echo "Unloading $NAME ($@)"
	fi

	/sbin/rmmod $NAME || ERROR="Failed to unload $NAME" return 1

	return 0
}

unload_modules() {
	local MODULES_REVERSE=( $(echo ${MODULES[@]} |
		awk '{for (i=NF;i>=1;i--) printf $i" "} END{print ""}') )

	for MOD in ${MODULES_REVERSE[*]}; do
		local NAME=`basename ${MOD} .ko`

		if /sbin/lsmod | egrep -q "^${NAME}"; then

			if [ "${DUMP_LOG}" -a ${NAME} = "spl" ]; then
				spl_dump_log
			fi

			unload_module ${MOD} || return 1
		fi
	done

	if [ ${VERBOSE} ]; then
		echo "Successfully unloaded ZFS module stack"
	fi

	return 0
}
