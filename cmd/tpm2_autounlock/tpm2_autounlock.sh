#! /bin/bash

# Default variables
	# If password is stored in "defaultpassword" field, ensure the security of the file.
defaultpassword="" 
defaulttpmindex="0x1800016"
defaulttpmpcrs="sha256:0,1,2,3,8,9"

helpmsg() {
	echo "tpm2_autounlock.sh -- configure the TPM 2.0 chip for zfs autounlock"
	echo ""
	echo "Usage: <-c, -v, or -n>  -i <val>, -p <val>, -P <val>, -r <val>"
	echo ""
	echo "Modes: (choose up to one)"
	echo "-c or --clearonly	Clear TPM and ZFS properties only"
	echo "-v or --verifyonly	Check current contents of TPM"
	echo "-n or --nvlockonly	Lock the ability to read password until reboot"
	echo "Not selecting any of the above will initialize the TPM for autounlock"
	echo ""
	echo "Options to override defaults:"
	echo "-i or --index		TPM NVRAM index (default: 0x1800016)"
	echo "-p or --password		Drive unlock password (default: <none provided>)"
	echo "-P or --pcrs		PCRS evaluated at unlock (default: sha256:0,1,2,3)"
	echo "-r or --rootfs		Rootfs being processed (default: current booted rootfs)"
	echo "The password can also be piped to tpm2_autounlock.sh, though '-p' takes precedence"
}

tpm_session_execute() {
	tpm2_startauthsession -Q ${define_policy} --session=s.dat
	tpm2_policypassword -Q --session=s.dat
	tpm2_policypcr -Q --pcr-list="$tpmpcrs" --session=s.dat --policy=policy.dat
	eval "${cmd}"
	returnval=$?
	tpm2_flushcontext s.dat
	rm s.dat
	rm policy.dat
	unset define_policy
	unset cmd
	return $returnval
}

# Check for root permissions
if [ $(id -u) -ne 0 ]; then
	echo "This script must be run as root"
	helpmsg
	exit 1
fi

# Check for presence of needed programs
command -v zfs >/dev/null 2>&1 &&
	[ $(zfs version | grep zfs-0 | awk -F 'zfs-0' '{print $2}' | awk -F '.' '{print $2}') -ge 8 ] \
		|| { echo >&2 "zfs 0.8 or greater required, but not found.  Aborting."; exit 1; }

for i in tpm2_startauthsession tpm2_policypassword tpm2_policypcr tpm2_flushcontext tpm2_nvdefine tpm2_nvwrite tpm2_nvread; do
	command -v $i >/dev/null 2>&1 &&
	[ $($i -v | awk -F 'version=' '{print $2}' | awk -F '"' '{print $2}' | awk -F '.' '{print $1}') -ge 4 ] \
		|| { echo >&2 "'$i' version 4.0 or greater required, but not found.  Aborting."; exit 1; }
done

# Check for presence of TPM 2.0 Hardware
if [ $(cat /sys/module/tpm/version) != "2.0" ]; then
	echo "TPM 2.0 not found on system"
	exit 1
fi

# Process user entered arguments
while [ "$1" != "" ]; do
	case $1 in
		-i | --index ) shift; tpmindex=$1;;
		-P | --pcrs ) shift; tpmpcrs=$1;;
		-r | --rootfs ) shift; rootfs=$1;;
		-p | --password ) shift; password=$1;;
		-v | --verifyonly ) verifyonly=1;;
		-c | --clearonly ) clearonly=1;;
		-n | --nvlockonly ) nvlockonly=1;;
		-h | --help ) helpmsg; exit 0;;
		* ) helpmsg; exit 1;;
	esac
	shift
done

# Use -r or --rootfs value if specified, otherwise try to detect running rootfs
if [ -z "$rootfs" ]; then
	rootfs=$(zfs mount |awk '$2 ==  "/" { print $1 }')
	if [ -z "$rootfs" ]; then
		echo "Rootfs not specified (-r / --rootfs) and current rootfs could not be determined"
		exit 1
	fi
fi

# With rootfs determined, find the encryptionroot and its guid
ENCRYPTIONROOT=$(zfs get -H -o value encryptionroot "${rootfs}")
rootguid=$(zfs get guid -o value -H "${ENCRYPTIONROOT}")

# Use -i or --index value if specified, otherwise look for existing property, and finally fallback to script default
if [ -z "$tpmindex" ]; then
	tpmindex=$(zfs get -H -o value org.zfsonlinux.tpm2:index "${ENCRYPTIONROOT}")
	if [ "$tpmindex" = "-" ]; then
		tpmindex="$defaulttpmindex"
	fi
fi

# Use -P or --pcrs value if specified, otherwise look for existing property, and finally fallback to script default
if [ -z "$tpmpcrs" ]; then
	tpmpcrs=$(zfs get -H -o value org.zfsonlinux.tpm2:pcrs "${ENCRYPTIONROOT}")
	if [ "$tpmpcrs" = "-" ]; then
		tpmpcrs="$defaulttpmpcrs"
	fi
fi

# If password is needed, use argument, piped, "script default" or stdin, in that order
if [ -z "$password" ] && (( verifyonly+clearonly+nvlockonly == 0)); then
	[[ -p /dev/stdin ]] && { mapfile -t; set -- "${MAPFILE[@]}"; set -- $@; }
	pipepassword="$@"
	if [ -n "$pipepassword" ]; then
		password="$pipepassword"
	elif [ -n "$defaultpassword" ]; then
		password="$defaultpassword"
	else
		while true; do
			read -s -p "Drive unlock password: " password
			echo
   			read -s -p "Confirm Password: " password2
			echo
  			[ "$password" = "$password2" ] && break
    			echo "Please try again"
		done
	fi	
fi

# Check for invalid arguments
if (( verifyonly+clearonly+nvlockonly > 1)) ; then
	echo " You can only select only one of the following: verifyonly, clearonly, or nvlockonly"
	exit 1
fi

########################################
# Start execution
if [ $clearonly ]; then
	tpm2_nvundefine "$tpmindex" >/dev/null 2>&1
	zfs inherit org.zfsonlinux.tpm2:index "${ENCRYPTIONROOT}"
	zfs inherit org.zfsonlinux.tpm2:pcrs "${ENCRYPTIONROOT}"

elif [ $verifyonly ]; then
	echo "Attempting to read TPM index '$tpmindex' locked with pcrs '$tpmpcrs' and guid '$rootguid'."
	define_policy="--policy"
	cmd="tpm2_nvread ${tpmindex} --auth=session:s.dat+${rootguid}"
	tpm_session_execute
	if [ $? = 0 ]; then
		echo "is the stored password."
#	else
		# The tpm2_nvread stdout messages will be visible to user
	fi

elif [ $nvlockonly ]; then
	define_policy="--policy"
	cmd="tpm2_nvreadlock  ${tpmindex} --auth=session:s.dat+${rootguid}"
	tpm_session_execute
else
	# Preemptively clear the tpm index
	tpm2_nvundefine "$tpmindex" >/dev/null 2>&1

	# Define NVRAM location and its access rules
	cmd="tpm2_nvdefine ${tpmindex} -Q --hierarchy=o --index-auth=${rootguid} --size=512 --policy=policy.dat --attributes='policyread|policywrite|read_stclear'"
	tpm_session_execute

	# Write password to NVRAM location
	define_policy="--policy"
	cmd="echo $password | tpm2_nvwrite ${tpmindex} -Q --auth=session:s.dat+${rootguid} --input=-"
	tpm_session_execute
	echo "Note: Password storage can be verified with '--verifyonly' until reboot or locked manually with '--nvlockonly'."

	# Store zfs filesystem properties
	zfs set org.zfsonlinux.tpm2:index="$tpmindex" "${ENCRYPTIONROOT}"
	zfs set org.zfsonlinux.tpm2:pcrs="$tpmpcrs" "${ENCRYPTIONROOT}"
fi
