#!/bin/bash -e
# pi-gen stage prerun: start from the previous stage's root file system.
if [ ! -d "${ROOTFS_DIR}" ]; then
  copy_previous
fi
