#!/bin/sh

launchctl unload /Library/LaunchDaemons/org.openzfsonosx.ZetaAuthorizationHelper.plist
rm /Library/LaunchDaemons/org.openzfsonosx.ZetaAuthorizationHelper.plist
rm /Library/PrivilegedHelperTools/org.openzfsonosx.ZetaAuthorizationHelper
