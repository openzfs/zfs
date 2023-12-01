#!/bin/sh
# shellcheck disable=SC2154,SC3043
# zed-functions.sh
#
# ZED helper functions for use in ZEDLETs


# Variable Defaults
#
: "${ZED_LOCKDIR:="/var/lock"}"
: "${ZED_NOTIFY_INTERVAL_SECS:=3600}"
: "${ZED_NOTIFY_VERBOSE:=0}"
: "${ZED_RUNDIR:="/var/run"}"
: "${ZED_SYSLOG_PRIORITY:="daemon.notice"}"
: "${ZED_SYSLOG_TAG:="zed"}"

ZED_FLOCK_FD=8


# zed_check_cmd (cmd, ...)
#
# For each argument given, search PATH for the executable command [cmd].
# Log a message if [cmd] is not found.
#
# Arguments
#   cmd: name of executable command for which to search
#
# Return
#   0 if all commands are found in PATH and are executable
#   n for a count of the command executables that are not found
#
zed_check_cmd()
{
    local cmd
    local rv=0

    for cmd; do
        if ! command -v "${cmd}" >/dev/null 2>&1; then
            zed_log_err "\"${cmd}\" not installed"
            rv=$((rv + 1))
        fi
    done
    return "${rv}"
}


# zed_log_msg (msg, ...)
#
# Write all argument strings to the system log.
#
# Globals
#   ZED_SYSLOG_PRIORITY
#   ZED_SYSLOG_TAG
#
# Return
#   nothing
#
zed_log_msg()
{
    logger -p "${ZED_SYSLOG_PRIORITY}" -t "${ZED_SYSLOG_TAG}" -- "$@"
}


# zed_log_err (msg, ...)
#
# Write an error message to the system log.  This message will contain the
# script name, EID, and all argument strings.
#
# Globals
#   ZED_SYSLOG_PRIORITY
#   ZED_SYSLOG_TAG
#   ZEVENT_EID
#
# Return
#   nothing
#
zed_log_err()
{
    zed_log_msg "error: ${0##*/}:""${ZEVENT_EID:+" eid=${ZEVENT_EID}:"}" "$@"
}


# zed_lock (lockfile, [fd])
#
# Obtain an exclusive (write) lock on [lockfile].  If the lock cannot be
# immediately acquired, wait until it becomes available.
#
# Every zed_lock() must be paired with a corresponding zed_unlock().
#
# By default, flock-style locks associate the lockfile with file descriptor 8.
# The bash manpage warns that file descriptors >9 should be used with care as
# they may conflict with file descriptors used internally by the shell.  File
# descriptor 9 is reserved for zed_rate_limit().  If concurrent locks are held
# within the same process, they must use different file descriptors (preferably
# decrementing from 8); otherwise, obtaining a new lock with a given file
# descriptor will release the previous lock associated with that descriptor.
#
# Arguments
#   lockfile: pathname of the lock file; the lock will be stored in
#     ZED_LOCKDIR unless the pathname contains a "/".
#   fd: integer for the file descriptor used by flock (OPTIONAL unless holding
#     concurrent locks)
#
# Globals
#   ZED_FLOCK_FD
#   ZED_LOCKDIR
#
# Return
#   nothing
#
zed_lock()
{
    local lockfile="$1"
    local fd="${2:-${ZED_FLOCK_FD}}"
    local umask_bak
    local err

    [ -n "${lockfile}" ] || return
    if ! expr "${lockfile}" : '.*/' >/dev/null 2>&1; then
        lockfile="${ZED_LOCKDIR}/${lockfile}"
    fi

    umask_bak="$(umask)"
    umask 077

    # Obtain a lock on the file bound to the given file descriptor.
    #
    eval "exec ${fd}>> '${lockfile}'"
    if ! err="$(flock --exclusive "${fd}" 2>&1)"; then
        zed_log_err "failed to lock \"${lockfile}\": ${err}"
    fi

    umask "${umask_bak}"
}


# zed_unlock (lockfile, [fd])
#
# Release the lock on [lockfile].
#
# Arguments
#   lockfile: pathname of the lock file
#   fd: integer for the file descriptor used by flock (must match the file
#     descriptor passed to the zed_lock function call)
#
# Globals
#   ZED_FLOCK_FD
#   ZED_LOCKDIR
#
# Return
#   nothing
#
zed_unlock()
{
    local lockfile="$1"
    local fd="${2:-${ZED_FLOCK_FD}}"
    local err

    [ -n "${lockfile}" ] || return
    if ! expr "${lockfile}" : '.*/' >/dev/null 2>&1; then
        lockfile="${ZED_LOCKDIR}/${lockfile}"
    fi

    # Release the lock and close the file descriptor.
    if ! err="$(flock --unlock "${fd}" 2>&1)"; then
        zed_log_err "failed to unlock \"${lockfile}\": ${err}"
    fi
    eval "exec ${fd}>&-"
}


# zed_notify (subject, pathname)
#
# Send a notification via all available methods.
#
# Arguments
#   subject: notification subject
#   pathname: pathname containing the notification message (OPTIONAL)
#
# Return
#   0: notification succeeded via at least one method
#   1: notification failed
#   2: no notification methods configured
#
zed_notify()
{
    local subject="$1"
    local pathname="$2"
    local num_success=0
    local num_failure=0

    zed_notify_email "${subject}" "${pathname}"; rv=$?
    [ "${rv}" -eq 0 ] && num_success=$((num_success + 1))
    [ "${rv}" -eq 1 ] && num_failure=$((num_failure + 1))

    zed_notify_pushbullet "${subject}" "${pathname}"; rv=$?
    [ "${rv}" -eq 0 ] && num_success=$((num_success + 1))
    [ "${rv}" -eq 1 ] && num_failure=$((num_failure + 1))

    zed_notify_slack_webhook "${subject}" "${pathname}"; rv=$?
    [ "${rv}" -eq 0 ] && num_success=$((num_success + 1))
    [ "${rv}" -eq 1 ] && num_failure=$((num_failure + 1))

    zed_notify_pushover "${subject}" "${pathname}"; rv=$?
    [ "${rv}" -eq 0 ] && num_success=$((num_success + 1))
    [ "${rv}" -eq 1 ] && num_failure=$((num_failure + 1))

    zed_notify_ntfy "${subject}" "${pathname}"; rv=$?
    [ "${rv}" -eq 0 ] && num_success=$((num_success + 1))
    [ "${rv}" -eq 1 ] && num_failure=$((num_failure + 1))

    [ "${num_success}" -gt 0 ] && return 0
    [ "${num_failure}" -gt 0 ] && return 1
    return 2
}


# zed_notify_email (subject, pathname)
#
# Send a notification via email to the address specified by ZED_EMAIL_ADDR.
#
# Requires the mail executable to be installed in the standard PATH, or
# ZED_EMAIL_PROG to be defined with the pathname of an executable capable of
# reading a message body from stdin.
#
# Command-line options to the mail executable can be specified in
# ZED_EMAIL_OPTS.  This undergoes the following keyword substitutions:
# - @ADDRESS@ is replaced with the space-delimited recipient email address(es)
# - @SUBJECT@ is replaced with the notification subject
#   If @SUBJECT@ was omited here, a "Subject: ..." header will be added to notification
#
#
# Arguments
#   subject: notification subject
#   pathname: pathname containing the notification message (OPTIONAL)
#
# Globals
#   ZED_EMAIL_PROG
#   ZED_EMAIL_OPTS
#   ZED_EMAIL_ADDR
#
# Return
#   0: notification sent
#   1: notification failed
#   2: not configured
#
zed_notify_email()
{
    local subject="${1:-"ZED notification"}"
    local pathname="${2:-"/dev/null"}"

    : "${ZED_EMAIL_PROG:="mail"}"
    : "${ZED_EMAIL_OPTS:="-s '@SUBJECT@' @ADDRESS@"}"

    # For backward compatibility with ZED_EMAIL.
    if [ -n "${ZED_EMAIL}" ] && [ -z "${ZED_EMAIL_ADDR}" ]; then
        ZED_EMAIL_ADDR="${ZED_EMAIL}"
    fi
    [ -n "${ZED_EMAIL_ADDR}" ] || return 2

    zed_check_cmd "${ZED_EMAIL_PROG}" || return 1

    [ -n "${subject}" ] || return 1
    if [ ! -r "${pathname}" ]; then
        zed_log_err \
                "${ZED_EMAIL_PROG##*/} cannot read \"${pathname}\""
        return 1
    fi

    # construct cmdline options
    ZED_EMAIL_OPTS_PARSED="$(echo "${ZED_EMAIL_OPTS}" \
        | sed   -e "s/@ADDRESS@/${ZED_EMAIL_ADDR}/g" \
                -e "s/@SUBJECT@/${subject}/g")"

    # pipe message to email prog
    # shellcheck disable=SC2086,SC2248
    {
        # no subject passed as option?
        if [ "${ZED_EMAIL_OPTS%@SUBJECT@*}" = "${ZED_EMAIL_OPTS}" ] ; then
            # inject subject header
            printf "Subject: %s\n" "${subject}"
        fi
        # output message
        cat "${pathname}"
    } |
    eval ${ZED_EMAIL_PROG} ${ZED_EMAIL_OPTS_PARSED} >/dev/null 2>&1
    rv=$?
    if [ "${rv}" -ne 0 ]; then
        zed_log_err "${ZED_EMAIL_PROG##*/} exit=${rv}"
        return 1
    fi
    return 0
}


# zed_notify_pushbullet (subject, pathname)
#
# Send a notification via Pushbullet <https://www.pushbullet.com/>.
# The access token (ZED_PUSHBULLET_ACCESS_TOKEN) identifies this client to the
# Pushbullet server.  The optional channel tag (ZED_PUSHBULLET_CHANNEL_TAG) is
# for pushing to notification feeds that can be subscribed to; if a channel is
# not defined, push notifications will instead be sent to all devices
# associated with the account specified by the access token.
#
# Requires awk, curl, and sed executables to be installed in the standard PATH.
#
# References
#   https://docs.pushbullet.com/
#   https://www.pushbullet.com/security
#
# Arguments
#   subject: notification subject
#   pathname: pathname containing the notification message (OPTIONAL)
#
# Globals
#   ZED_PUSHBULLET_ACCESS_TOKEN
#   ZED_PUSHBULLET_CHANNEL_TAG
#
# Return
#   0: notification sent
#   1: notification failed
#   2: not configured
#
zed_notify_pushbullet()
{
    local subject="$1"
    local pathname="${2:-"/dev/null"}"
    local msg_body
    local msg_tag
    local msg_json
    local msg_out
    local msg_err
    local url="https://api.pushbullet.com/v2/pushes"

    [ -n "${ZED_PUSHBULLET_ACCESS_TOKEN}" ] || return 2

    [ -n "${subject}" ] || return 1
    if [ ! -r "${pathname}" ]; then
        zed_log_err "pushbullet cannot read \"${pathname}\""
        return 1
    fi

    zed_check_cmd "awk" "curl" "sed" || return 1

    # Escape the following characters in the message body for JSON:
    # newline, backslash, double quote, horizontal tab, vertical tab,
    # and carriage return.
    #
    msg_body="$(awk '{ ORS="\\n" } { gsub(/\\/, "\\\\"); gsub(/"/, "\\\"");
        gsub(/\t/, "\\t"); gsub(/\f/, "\\f"); gsub(/\r/, "\\r"); print }' \
        "${pathname}")"

    # Push to a channel if one is configured.
    #
    [ -n "${ZED_PUSHBULLET_CHANNEL_TAG}" ] && msg_tag="$(printf \
        '"channel_tag": "%s", ' "${ZED_PUSHBULLET_CHANNEL_TAG}")"

    # Construct the JSON message for pushing a note.
    #
    msg_json="$(printf '{%s"type": "note", "title": "%s", "body": "%s"}' \
        "${msg_tag}" "${subject}" "${msg_body}")"

    # Send the POST request and check for errors.
    #
    msg_out="$(curl -u "${ZED_PUSHBULLET_ACCESS_TOKEN}:" -X POST "${url}" \
        --header "Content-Type: application/json" --data-binary "${msg_json}" \
        2>/dev/null)"; rv=$?
    if [ "${rv}" -ne 0 ]; then
        zed_log_err "curl exit=${rv}"
        return 1
    fi
    msg_err="$(echo "${msg_out}" \
        | sed -n -e 's/.*"error" *:.*"message" *: *"\([^"]*\)".*/\1/p')"
    if [ -n "${msg_err}" ]; then
        zed_log_err "pushbullet \"${msg_err}"\"
        return 1
    fi
    return 0
}


# zed_notify_slack_webhook (subject, pathname)
#
# Notification via Slack Webhook <https://api.slack.com/incoming-webhooks>.
# The Webhook URL (ZED_SLACK_WEBHOOK_URL) identifies this client to the
# Slack channel.
#
# Requires awk, curl, and sed executables to be installed in the standard PATH.
#
# References
#   https://api.slack.com/incoming-webhooks
#
# Arguments
#   subject: notification subject
#   pathname: pathname containing the notification message (OPTIONAL)
#
# Globals
#   ZED_SLACK_WEBHOOK_URL
#
# Return
#   0: notification sent
#   1: notification failed
#   2: not configured
#
zed_notify_slack_webhook()
{
    [ -n "${ZED_SLACK_WEBHOOK_URL}" ] || return 2

    local subject="$1"
    local pathname="${2:-"/dev/null"}"
    local msg_body
    local msg_tag
    local msg_json
    local msg_out
    local msg_err
    local url="${ZED_SLACK_WEBHOOK_URL}"

    [ -n "${subject}" ] || return 1
    if [ ! -r "${pathname}" ]; then
        zed_log_err "slack webhook cannot read \"${pathname}\""
        return 1
    fi

    zed_check_cmd "awk" "curl" "sed" || return 1

    # Escape the following characters in the message body for JSON:
    # newline, backslash, double quote, horizontal tab, vertical tab,
    # and carriage return.
    #
    msg_body="$(awk '{ ORS="\\n" } { gsub(/\\/, "\\\\"); gsub(/"/, "\\\"");
        gsub(/\t/, "\\t"); gsub(/\f/, "\\f"); gsub(/\r/, "\\r"); print }' \
        "${pathname}")"

    # Construct the JSON message for posting.
    #
    msg_json="$(printf '{"text": "*%s*\\n%s"}' "${subject}" "${msg_body}" )"

    # Send the POST request and check for errors.
    #
    msg_out="$(curl -X POST "${url}" \
        --header "Content-Type: application/json" --data-binary "${msg_json}" \
        2>/dev/null)"; rv=$?
    if [ "${rv}" -ne 0 ]; then
        zed_log_err "curl exit=${rv}"
        return 1
    fi
    msg_err="$(echo "${msg_out}" \
        | sed -n -e 's/.*"error" *:.*"message" *: *"\([^"]*\)".*/\1/p')"
    if [ -n "${msg_err}" ]; then
        zed_log_err "slack webhook \"${msg_err}"\"
        return 1
    fi
    return 0
}

# zed_notify_pushover (subject, pathname)
#
# Send a notification via Pushover <https://pushover.net/>.
# The access token (ZED_PUSHOVER_TOKEN) identifies this client to the
# Pushover server. The user token (ZED_PUSHOVER_USER) defines the user or
# group to which the notification will be sent.
#
# Requires curl and sed executables to be installed in the standard PATH.
#
# References
#   https://pushover.net/api
#
# Arguments
#   subject: notification subject
#   pathname: pathname containing the notification message (OPTIONAL)
#
# Globals
#   ZED_PUSHOVER_TOKEN
#   ZED_PUSHOVER_USER
#
# Return
#   0: notification sent
#   1: notification failed
#   2: not configured
#
zed_notify_pushover()
{
    local subject="$1"
    local pathname="${2:-"/dev/null"}"
    local msg_body
    local msg_out
    local msg_err
    local url="https://api.pushover.net/1/messages.json"

    [ -n "${ZED_PUSHOVER_TOKEN}" ] && [ -n "${ZED_PUSHOVER_USER}" ] || return 2

    if [ ! -r "${pathname}" ]; then
        zed_log_err "pushover cannot read \"${pathname}\""
        return 1
    fi

    zed_check_cmd "curl" "sed" || return 1

    # Read the message body in.
    #
    msg_body="$(cat "${pathname}")"

    if [ -z "${msg_body}" ]
    then
        msg_body=$subject
        subject=""
    fi

    # Send the POST request and check for errors.
    #
    msg_out="$( \
        curl \
        --form-string "token=${ZED_PUSHOVER_TOKEN}" \
        --form-string "user=${ZED_PUSHOVER_USER}" \
        --form-string "message=${msg_body}" \
        --form-string "title=${subject}" \
        "${url}" \
        2>/dev/null \
        )"; rv=$?
    if [ "${rv}" -ne 0 ]; then
        zed_log_err "curl exit=${rv}"
        return 1
    fi
    msg_err="$(echo "${msg_out}" \
        | sed -n -e 's/.*"errors" *:.*\[\(.*\)\].*/\1/p')"
    if [ -n "${msg_err}" ]; then
        zed_log_err "pushover \"${msg_err}"\"
        return 1
    fi
    return 0
}


# zed_notify_ntfy (subject, pathname)
#
# Send a notification via Ntfy.sh <https://ntfy.sh/>.
# The ntfy topic (ZED_NTFY_TOPIC) identifies the topic that the notification
# will be sent to Ntfy.sh server. The ntfy url (ZED_NTFY_URL) defines the
# self-hosted or provided hosted ntfy service location. The ntfy access token
# <https://docs.ntfy.sh/publish/#access-tokens> (ZED_NTFY_ACCESS_TOKEN) reprsents an
# access token that could be used if a topic is read/write protected. If a
# topic can be written to publicaly, a ZED_NTFY_ACCESS_TOKEN is not required.
#
# Requires curl and sed executables to be installed in the standard PATH.
#
# References
#   https://docs.ntfy.sh
#
# Arguments
#   subject: notification subject
#   pathname: pathname containing the notification message (OPTIONAL)
#
# Globals
#   ZED_NTFY_TOPIC
#   ZED_NTFY_ACCESS_TOKEN (OPTIONAL)
#   ZED_NTFY_URL
#
# Return
#   0: notification sent
#   1: notification failed
#   2: not configured
#
zed_notify_ntfy()
{
    local subject="$1"
    local pathname="${2:-"/dev/null"}"
    local msg_body
    local msg_out
    local msg_err

    [ -n "${ZED_NTFY_TOPIC}" ] || return 2
    local url="${ZED_NTFY_URL:-"https://ntfy.sh"}/${ZED_NTFY_TOPIC}"

    if [ ! -r "${pathname}" ]; then
        zed_log_err "ntfy cannot read \"${pathname}\""
        return 1
    fi

    zed_check_cmd "curl" "sed" || return 1

    # Read the message body in.
    #
    msg_body="$(cat "${pathname}")"

    if [ -z "${msg_body}" ]
    then
        msg_body=$subject
        subject=""
    fi

    # Send the POST request and check for errors.
    #
    if [ -n "${ZED_NTFY_ACCESS_TOKEN}" ]; then
        msg_out="$( \
        curl \
        -u ":${ZED_NTFY_ACCESS_TOKEN}" \
        -H "Title: ${subject}" \
        -d "${msg_body}" \
        -H "Priority: high" \
        "${url}" \
        2>/dev/null \
        )"; rv=$?
    else
        msg_out="$( \
        curl \
        -H "Title: ${subject}" \
        -d "${msg_body}" \
        -H "Priority: high" \
        "${url}" \
        2>/dev/null \
        )"; rv=$?
    fi
    if [ "${rv}" -ne 0 ]; then
        zed_log_err "curl exit=${rv}"
        return 1
    fi
    msg_err="$(echo "${msg_out}" \
        | sed -n -e 's/.*"errors" *:.*\[\(.*\)\].*/\1/p')"
    if [ -n "${msg_err}" ]; then
        zed_log_err "ntfy \"${msg_err}"\"
        return 1
    fi
    return 0
}



# zed_rate_limit (tag, [interval])
#
# Check whether an event of a given type [tag] has already occurred within the
# last [interval] seconds.
#
# This function obtains a lock on the statefile using file descriptor 9.
#
# Arguments
#   tag: arbitrary string for grouping related events to rate-limit
#   interval: time interval in seconds (OPTIONAL)
#
# Globals
#   ZED_NOTIFY_INTERVAL_SECS
#   ZED_RUNDIR
#
# Return
#   0 if the event should be processed
#   1 if the event should be dropped
#
# State File Format
#   time;tag
#
zed_rate_limit()
{
    local tag="$1"
    local interval="${2:-${ZED_NOTIFY_INTERVAL_SECS}}"
    local lockfile="zed.zedlet.state.lock"
    local lockfile_fd=9
    local statefile="${ZED_RUNDIR}/zed.zedlet.state"
    local time_now
    local time_prev
    local umask_bak
    local rv=0

    [ -n "${tag}" ] || return 0

    zed_lock "${lockfile}" "${lockfile_fd}"
    time_now="$(date +%s)"
    time_prev="$(grep -E "^[0-9]+;${tag}\$" "${statefile}" 2>/dev/null \
        | tail -1 | cut -d\; -f1)"

    if [ -n "${time_prev}" ] \
            && [ "$((time_now - time_prev))" -lt "${interval}" ]; then
        rv=1
    else
        umask_bak="$(umask)"
        umask 077
        grep -E -v "^[0-9]+;${tag}\$" "${statefile}" 2>/dev/null \
            > "${statefile}.$$"
        echo "${time_now};${tag}" >> "${statefile}.$$"
        mv -f "${statefile}.$$" "${statefile}"
        umask "${umask_bak}"
    fi

    zed_unlock "${lockfile}" "${lockfile_fd}"
    return "${rv}"
}


# zed_guid_to_pool (guid)
#
# Convert a pool GUID into its pool name (like "tank")
# Arguments
#   guid: pool GUID (decimal or hex)
#
# Return
#   Pool name
#
zed_guid_to_pool()
{
	if [ -z "$1" ] ; then
		return
	fi

	guid="$(printf "%u" "$1")"
	$ZPOOL get -H -ovalue,name guid | awk '$1 == '"$guid"' {print $2; exit}'
}

# zed_exit_if_ignoring_this_event
#
# Exit the script if we should ignore this event, as determined by
# $ZED_SYSLOG_SUBCLASS_INCLUDE and $ZED_SYSLOG_SUBCLASS_EXCLUDE in zed.rc.
# This function assumes you've imported the normal zed variables.
zed_exit_if_ignoring_this_event()
{
	if [ -n "${ZED_SYSLOG_SUBCLASS_INCLUDE}" ]; then
	    eval "case ${ZEVENT_SUBCLASS} in
	    ${ZED_SYSLOG_SUBCLASS_INCLUDE});;
	    *) exit 0;;
	    esac"
	elif [ -n "${ZED_SYSLOG_SUBCLASS_EXCLUDE}" ]; then
	    eval "case ${ZEVENT_SUBCLASS} in
	    ${ZED_SYSLOG_SUBCLASS_EXCLUDE}) exit 0;;
	    *);;
	    esac"
	fi
}
