# zed-functions.ps1
#
# 2024 Jorgen Lundman <lundman@lundman.net>
#

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
function zed_log_msg
{
    param (
        [string]$level,
        [string]$message
    )

    $eventSource = "OpenZFS_zed"
    $eventLog = "Application"

    # Ensure the event source exists
    try {
        if (-not [System.Diagnostics.EventLog]::SourceExists($eventSource)) {
            [System.Diagnostics.EventLog]::CreateEventSource($eventSource, $eventLog)
        }
    } catch {
        Write-Host "WARNING: Insufficient permissions to check or create event log source. Falling back to console output."
        $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
        Write-Host "$timestamp [$level] $message"
        return
    }

    # Write the event log
    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    $fullMessage = "$timestamp [$level] $message"
    try {
        Write-EventLog -LogName $eventLog -Source $eventSource -EventId 0 -EntryType Information -Message $fullMessage
    } catch {
        Write-Host "ERROR: Unable to write to event log. Falling back to console output."
        Write-Host "$timestamp [$level] $message"
    }
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
function zed_log_err
{
    param (
        [string]$message
    )

    $eventSource = "OpenZFS_zed"
    $eventLog = "Application"

    # Ensure the event source exists
    try {
        if (-not [System.Diagnostics.EventLog]::SourceExists($eventSource)) {
            [System.Diagnostics.EventLog]::CreateEventSource($eventSource, $eventLog)
        }
    } catch {
        Write-Host "WARNING: Insufficient permissions to check or create event log source. Falling back to console output."
        $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
        Write-Host "$timestamp [ERROR] $message"
        return
    }

    # Write the event log
    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    $fullMessage = "$timestamp [ERROR] $message"
    try {
        Write-EventLog -LogName $eventLog -Source $eventSource -EventId 0 -EntryType Error -Message $fullMessage
    } catch {
        Write-Host "ERROR: Unable to write to event log. Falling back to console output."
        Write-Host "$timestamp [ERROR] $message"
    }
}

# For real notifications to work, run in powershell (Admin)
# "Install-Module -Name BurntToast -Force -AllowClobber"
# Failing that, fall back to a regular MessageBox
function zed_notify_burnttoast()
{
    param (
        [string]$Title = "Notification",
        [string]$Message = "This is a test notification."
    )

	if (-not $env:ZED_BURNTTOAST_NOTIFICATIONS) {
		return 2
	}

    # Check if the BurntToast module is available
    if (Get-Module -ListAvailable -Name BurntToast) {
        try {
            # Import the module
            Import-Module BurntToast -ErrorAction Stop
            
            # Create a BurntToast notification
            New-BurntToastNotification -Text $Title, $Message
        }
        catch {
            # If there's an error, fall back to a basic notification
            Write-Host "BurntToast failed to create a notification: $_"
        }
    }
    else {
        # Fallback if BurntToast is not installed
        Write-Host "BurntToast module is not installed. Falling back to basic notification."
    }
}

function zed_notify_toast
{
    # If the toast notification was successfully sent, return success
    return 0
}

# Function to show a message box
function zed_notify_messagebox
{
    param (
        [string]$Message,
        [string]$Title
    )
    
    # Load the necessary assembly
    Add-Type -AssemblyName System.Windows.Forms
    
    # Display the message box
    [System.Windows.Forms.MessageBox]::Show($Message, $Title)
}


# zed_notify (title, message)
#
# Send a notification via all available methods.
#
# Arguments
#   title: notification title
#   message: notification message (OPTIONAL)
#
# Return
#   0: notification succeeded via at least one method
#   1: notification failed
#   2: no notification methods configured
#
function zed_notify
{
    param (
        [string]$title,
        [string]$message
    )
    $num_success=0
    $num_failure=0

    # Call zed_notify_toast (Windows-specific toast notification)
    $rv = zed_notify_toast "$subject" "$pathname"
	if ($rv -eq 0) {
        $num_success += 1
    } elseif ($rv -eq 1) {
        $num_failure += 1
    }

	$rv = zed_notify_burnttoast "$subject" "$pathname"
	if ($rv -eq 0) {
        $num_success += 1
    } elseif ($rv -eq 1) {
        $num_failure += 1
    }

    # Additional notification methods could go here (email, pushbullet, etc.)

    # If we had any success, return success code
  if ($num_success -gt 0) {
        return 0
    } elseif ($num_failure -gt 0) {
        return 1
    } else {
        return 2
    }
}


# zed_guid_to_pool (guid)
#
# Convert a pool GUID into its pool name (like "tank")
# Arguments
#   guid: pool GUID (decimal or hex)
#
# Return
#   Pool name
function zed_guid_to_pool
{
    param (
        [Parameter(Mandatory=$true)]
        [string]$guid
    )

    if (-not $guid) {
        return # Return nothing if no GUID is provided
    }

    # Ensure $env:ZPOOL is set
    if (-not $env:ZPOOL) {
        Write-Error "Environment variable ZPOOL is not set"
        return
    }

    try {
        # Run the ZPOOL command and filter by the GUID
        $poolName = & $env:ZPOOL get -H -o value,name guid 2>&1 | 
                    Where-Object { $_ -match $guid } |
                    ForEach-Object { ($_ -split '\s+')[1] }

        if ($poolName) {
            return $poolName
        } else {
            Write-Error "No pool found for GUID $guid"
        }
    } catch {
        Write-Error "Error querying pool name: $_"
    }
}
