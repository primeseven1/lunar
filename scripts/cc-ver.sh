#!/usr/bin/env bash

set -e

C_COMPILER=x86_64-elf-gcc
VERSION_STR=$("$C_COMPILER" -dumpversion 2>/dev/null)
if [[ -z "$VERSION_STR" ]]; then
	echo "Cannot get compiler version"
	exit 1
fi

MIN_MAJOR=12
MIN_MINOR=2
MAJOR=$(echo "$VERSION_STR" | cut -d. -f1)
MINOR=$(echo "$VERSION_STR" | cut -d. -f2)
if [[ -z "$MAJOR" || -z "$MINOR" ]]; then
	echo "Compiler major or minor is empty"
	exit 1
fi

if [[ "$MAJOR" -gt "$MIN_MAJOR" ]] || ([[ "$MAJOR" -eq "$MIN_MAJOR" ]] && [[ "$MINOR" -ge "$MIN_MINOR" ]]); then
	exit 0
fi

echo "Compiler version is too old"
echo "Current version: $MAJOR.$MINOR"
echo "Required version: $MIN_MAJOR.$MIN_MINOR"
exit 1
