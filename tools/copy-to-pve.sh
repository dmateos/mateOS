#!/usr/bin/env bash
set -euo pipefail

# Copy build artifacts to a Proxmox host.
#
# Required env:
#   PVE_HOST=user@hostname
#
# Optional env:
#   PVE_DEST=~/mateos-debug
#   KERNEL_PATH=./dmos.bin
#   BOOT_IMG_PATH=./boot.img

: "${PVE_HOST:?PVE_HOST is required (example: PVE_HOST=root@pve)}"

PVE_DEST="${PVE_DEST:-~/mateos-debug}"
KERNEL_PATH="${KERNEL_PATH:-./dmos.bin}"
BOOT_IMG_PATH="${BOOT_IMG_PATH:-./boot.img}"

if [[ ! -f "${KERNEL_PATH}" ]]; then
  echo "missing kernel: ${KERNEL_PATH}" >&2
  exit 1
fi
if [[ ! -f "${BOOT_IMG_PATH}" ]]; then
  echo "missing boot disk: ${BOOT_IMG_PATH}" >&2
  exit 1
fi

files=("${KERNEL_PATH}" "${BOOT_IMG_PATH}")

echo "Creating remote dir: ${PVE_HOST}:${PVE_DEST}"
ssh "${PVE_HOST}" "mkdir -p ${PVE_DEST}"

echo "Copying artifacts..."
rsync -az --progress "${files[@]}" "${PVE_HOST}:${PVE_DEST}/"

echo "Done."
