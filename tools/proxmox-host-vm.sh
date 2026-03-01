#!/usr/bin/env bash
set -euo pipefail

# Run on the Proxmox host.
# Assumes kernel and boot disk image are already in your home directory.
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
#   BOOT_IMG=$HOME/boot.img
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
if [[ -z "${BOOT_IMG:-}" ]]; then
  BOOT_IMG="$(resolve_first_existing \
    "${SCRIPT_DIR}/boot.img" \
    "${SCRIPT_DIR}/mateos-debug/boot.img" \
    "${PWD}/boot.img" \
    "${PWD}/mateos-debug/boot.img" \
    "${SUDO_HOME}/boot.img" \
    "${SUDO_HOME}/mateos-debug/boot.img" \
    "${HOME}/boot.img" \
    "${HOME}/mateos-debug/boot.img" || true)"
fi

if ! command -v "${QM_BIN}" >/dev/null 2>&1; then
  echo "missing ${QM_BIN}" >&2
  exit 1
fi

if [[ ! -f "${KERNEL}" ]]; then
  echo "missing kernel: ${KERNEL}" >&2
  exit 1
fi
if [[ ! -f "${BOOT_IMG}" ]]; then
  echo "missing boot disk: ${BOOT_IMG}" >&2
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

# Boot disk (FAT16) is always required — it contains all userland binaries
qemu_args="-kernel ${KERNEL} -drive file=${BOOT_IMG},format=raw,if=ide -no-reboot -no-shutdown"

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
