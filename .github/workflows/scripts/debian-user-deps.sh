#!/usr/bin/env bash

set -eu

sudo apt-get install -y \
  autoconf automake build-essential libblkid-dev libssl-dev libtirpc-dev \
  libtool libunwind-dev pkg-config uuid-dev zlib1g-dev "$@"
