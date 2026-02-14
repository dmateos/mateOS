#!/usr/bin/env bash
set -euo pipefail

# Run on the Proxmox host.
# Assumes kernel/initrd are already in your home directory.
#
# Example:
#   sudo VMID=9000 BRIDGE=vmbr0 bash tools/proxmox-host-vm.sh
#
# Config (env):
#   VMID=9000
#   VM_NAME=mateos-debug
#   MEMORY=512
#   CORES=1
#   SOCKETS=1
#   BRIDGE=vmbr0       # empty string disables net0
#   NIC_MODEL=rtl8139  # matches local qemu run target
#   KERNEL=$HOME/dmos.bin
#   INITRD=$HOME/initrd.img
#   INCLUDE_FAT16=0
#   FAT16_IMG=$HOME/fat16_test.img
#   RESTART=1
#   QM_BIN=qm

if [[ "${EUID}" -ne 0 ]]; then
  echo "run as root (or via sudo)" >&2
  exit 1
fi

resolve_first_existing() {
  for p in "$@"; do
    if [[ -f "${p}" ]]; then
      echo "${p}"
      return 0
    fi
  done
  return 1
}

SUDO_HOME=""
if [[ -n "${SUDO_USER:-}" ]]; then
  SUDO_HOME="$(getent passwd "${SUDO_USER}" | cut -d: -f6 || true)"
fi
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

VMID="${VMID:-9000}"
VM_NAME="${VM_NAME:-mateos-debug}"
MEMORY="${MEMORY:-512}"
CORES="${CORES:-1}"
SOCKETS="${SOCKETS:-1}"
BRIDGE="${BRIDGE:-vmbr0}"
NIC_MODEL="${NIC_MODEL:-rtl8139}"
INCLUDE_FAT16="${INCLUDE_FAT16:-0}"
RESTART="${RESTART:-1}"
QM_BIN="${QM_BIN:-qm}"

if [[ -z "${KERNEL:-}" ]]; then
  KERNEL="$(resolve_first_existing \
    "${SCRIPT_DIR}/dmos.bin" \
    "${SCRIPT_DIR}/mateos-debug/dmos.bin" \
    "${PWD}/dmos.bin" \
    "${PWD}/mateos-debug/dmos.bin" \
    "${SUDO_HOME}/dmos.bin" \
    "${SUDO_HOME}/mateos-debug/dmos.bin" \
    "${HOME}/dmos.bin" \
    "${HOME}/mateos-debug/dmos.bin" || true)"
fi
if [[ -z "${INITRD:-}" ]]; then
  INITRD="$(resolve_first_existing \
    "${SCRIPT_DIR}/initrd.img" \
    "${SCRIPT_DIR}/mateos-debug/initrd.img" \
    "${PWD}/initrd.img" \
    "${PWD}/mateos-debug/initrd.img" \
    "${SUDO_HOME}/initrd.img" \
    "${SUDO_HOME}/mateos-debug/initrd.img" \
    "${HOME}/initrd.img" \
    "${HOME}/mateos-debug/initrd.img" || true)"
fi
if [[ -z "${FAT16_IMG:-}" ]]; then
  FAT16_IMG="$(resolve_first_existing \
    "${SCRIPT_DIR}/fat16_test.img" \
    "${SCRIPT_DIR}/mateos-debug/fat16_test.img" \
    "${PWD}/fat16_test.img" \
    "${PWD}/mateos-debug/fat16_test.img" \
    "${SUDO_HOME}/fat16_test.img" \
    "${SUDO_HOME}/mateos-debug/fat16_test.img" \
    "${HOME}/fat16_test.img" \
    "${HOME}/mateos-debug/fat16_test.img" || true)"
fi

if ! command -v "${QM_BIN}" >/dev/null 2>&1; then
  echo "missing ${QM_BIN}" >&2
  exit 1
fi

if [[ ! -f "${KERNEL}" ]]; then
  echo "missing kernel: ${KERNEL}" >&2
  exit 1
fi
if [[ ! -f "${INITRD}" ]]; then
  echo "missing initrd: ${INITRD}" >&2
  exit 1
fi

if [[ "${INCLUDE_FAT16}" == "1" && ! -f "${FAT16_IMG}" ]]; then
  echo "missing fat16 image: ${FAT16_IMG}" >&2
  exit 1
fi

if ! "${QM_BIN}" status "${VMID}" >/dev/null 2>&1; then
  "${QM_BIN}" create "${VMID}" \
    --name "${VM_NAME}" \
    --memory "${MEMORY}" \
    --cores "${CORES}" \
    --sockets "${SOCKETS}" \
    --ostype l26 \
    --machine pc \
    --bios seabios \
    --vga std \
    --serial0 socket \
    --agent 0 \
    --onboot 0
fi

if [[ -n "${BRIDGE}" ]]; then
  "${QM_BIN}" set "${VMID}" --net0 "${NIC_MODEL},bridge=${BRIDGE}"
else
  "${QM_BIN}" set "${VMID}" --delete net0 >/dev/null 2>&1 || true
fi

qemu_args="-kernel ${KERNEL} -initrd ${INITRD} -no-reboot -no-shutdown"
if [[ "${INCLUDE_FAT16}" == "1" ]]; then
  qemu_args="${qemu_args} -drive file=${FAT16_IMG},format=raw,if=ide"
fi

"${QM_BIN}" set "${VMID}" --args "${qemu_args}"

if [[ "${RESTART}" == "1" ]]; then
  status="$("${QM_BIN}" status "${VMID}" | awk '{print $2}')"
  if [[ "${status}" == "running" ]]; then
    "${QM_BIN}" stop "${VMID}" --timeout 20 || "${QM_BIN}" reset "${VMID}"
  fi
fi

"${QM_BIN}" start "${VMID}" >/dev/null 2>&1 || true
"${QM_BIN}" status "${VMID}"
echo "VM ${VMID} configured and started."
