#!/bin/sh -x

set -eu

# remove 4GiB of images
sudo systemd-run docker system prune --force --all --volumes

# remove unused packages
sudo apt remove -q --purge firefox

# remove unused software
sudo systemd-run rm -rf \
  "$AGENT_TOOLSDIRECTORY" \
  /opt/* \
  /usr/local/* \
  /usr/share/az* \
  /usr/share/gradle* \
  /usr/share/miniconda \
  /usr/share/swift
