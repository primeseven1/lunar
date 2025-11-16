#!/usr/bin/env bash

set -e

source $(dirname "$0")/ver.sh

LINKER="x86_64-elf-ld"
VERSION_STR=$("$LINKER" --version | head -n 1 | grep -o '[0-9.]\+' 2>/dev/null)
if [[ -z "$VERSION_STR" ]]; then
	echo "Cannot get linker version"
	exit 1
fi

check_version "$LINKER" "$VERSION_STR" 2 39
