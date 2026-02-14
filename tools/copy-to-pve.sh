#!/usr/bin/env bash
set -euo pipefail

# Copy build artifacts to a Proxmox host.
#
# Required env:
#   PVE_HOST=user@hostname
#
# Optional env:
#   PVE_DEST=~/mateos-debug
#   INCLUDE_FAT16=0
#   KERNEL_PATH=./dmos.bin
#   INITRD_PATH=./initrd.img
#   FAT16_PATH=./fat16_test.img

: "${PVE_HOST:?PVE_HOST is required (example: PVE_HOST=root@pve)}"

PVE_DEST="${PVE_DEST:-~/mateos-debug}"
INCLUDE_FAT16="${INCLUDE_FAT16:-0}"
KERNEL_PATH="${KERNEL_PATH:-./dmos.bin}"
INITRD_PATH="${INITRD_PATH:-./initrd.img}"
FAT16_PATH="${FAT16_PATH:-./fat16_test.img}"

if [[ ! -f "${KERNEL_PATH}" ]]; then
  echo "missing kernel: ${KERNEL_PATH}" >&2
  exit 1
fi
if [[ ! -f "${INITRD_PATH}" ]]; then
  echo "missing initrd: ${INITRD_PATH}" >&2
  exit 1
fi

files=("${KERNEL_PATH}" "${INITRD_PATH}")
if [[ "${INCLUDE_FAT16}" == "1" ]]; then
  if [[ ! -f "${FAT16_PATH}" ]]; then
    echo "missing FAT16 image: ${FAT16_PATH}" >&2
    exit 1
  fi
  files+=("${FAT16_PATH}")
fi

echo "Creating remote dir: ${PVE_HOST}:${PVE_DEST}"
ssh "${PVE_HOST}" "mkdir -p ${PVE_DEST}"

echo "Copying artifacts..."
rsync -az --progress "${files[@]}" "${PVE_HOST}:${PVE_DEST}/"

echo "Done."
