#!/bin/powershell
# shellcheck disable=SC2154
#
# Log all environment variables to ZED_DEBUG_LOG.
#
# This can be a useful aid when developing/debugging ZEDLETs since it shows the
# environment variables defined for each zevent.

if (Test-Path "$env:ZED_ZEDLET_DIR\zed.rc.ps1") {
    . "$env:ZED_ZEDLET_DIR\zed.rc.ps1"
}
if (Test-Path "$env:ZED_ZEDLET_DIR\zed-functions.ps1") {
    . "$env:ZED_ZEDLET_DIR\zed-functions.ps1"
}

#
# system_boot.ps1
#

# Define the path to the zpool.cache file
$zpoolCachePath = "C:\Windows\System32\Drivers\zpool.cache"

# Check if the zpool.cache file exists
if (Test-Path $zpoolCachePath) {
    Write-Host "zpool.cache found. Importing ZFS pools..."
    zed_log_msg LOG_NOTICE "zpool.cache found. Importing ZFS pools"

    # Import ZFS pools using the zpool command and the cache file
    & "$env:ZPOOL" import -c "$zpoolCachePath" -a
    
    # Optionally, log the result or handle any output
    zed_log_msg LOG_NOTICE "ZFS Pools Imported."
	zed_notify "ZFS Pools Imported" "path"
} else {
    zed_log_msg LOG_NOTICE "zpool.cache not found. No pools imported."
}
