#!/usr/bin/env bash

set -e

DEVICE=$1
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ISO="$SCRIPT_DIR/../tools/testing/lunar.iso"

usage() {
	echo "Usage: $0 <block-device>"
	echo "Example: sudo $0 /dev/sdX"
	exit 1
}

[[ -z "${DEVICE}" ]] && usage
[[ $EUID -ne 0 ]] && { echo "Please run as root (sudo)."; exit 1; }
[[ ! -f "$ISO" ]] && { echo "Error: ISO not found at $ISO"; exit 1; }
[[ ! -b "$DEVICE" ]] && { echo "Error: $DEVICE is not a block device"; exit 1; }

if lsblk -nrpo NAME,MOUNTPOINT "$DEVICE" | grep -qv '^[^ ]* *$'; then
	echo "Error: $DEVICE or one of its partitions is mounted. Unmount it first."
	exit 1
fi

echo "Writing ${ISO} to ${DEVICE}..."
echo "WARNING: This will erase all data on $DEVICE"
read -p "Continue? (y/N): " confirm

if [[ "$confirm" != "y" && "$confirm" != "Y" ]]; then
	echo "Aborted."
	exit 1
fi

dd if="$ISO" of="$DEVICE" bs=4M oflag=direct conv=fsync status=progress
sync
