#!/usr/bin/env bash

make -j$(nproc) && sudo make install && sudo ldconfig && sudo modprobe -r zfs && sudo modprobe zfs


