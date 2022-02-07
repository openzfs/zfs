#!/bin/sh

set -eu

# remove 4GiB of images
sudo systemd-run docker system prune --force --all --volumes

# remove unused software
sudo systemd-run rm -rf \
  "$AGENT_TOOLSDIRECTORY" \
  /opt/* \
  /usr/local/* \
  /usr/share/az* \
  /usr/share/dotnet \
  /usr/share/gradle* \
  /usr/share/miniconda \
  /usr/share/swift \
  /var/lib/gems \
  /var/lib/mysql \
  /var/lib/snapd
