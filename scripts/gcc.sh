#!/usr/bin/env bash

set -e
set -u

usage() {
	echo "Usage: $0 --gcc <VERSION> --binutils <VERSION> --prefix <DIRECTORY> --jobs <COUNT>"
	exit 1
}

START_DIR="$(pwd)"
GNUPGHOME="$(mktemp -d)"

cleanup() {
	echo -e "\nCleaning up..."
	cd "$START_DIR"
	rm -rf ./cross-compiler
	rm -f ./gnu-keyring.gpg
	echo "Temp directory can be safely removed: $GNUPGHOME"
}

trap cleanup EXIT

# This script can be used for other targets, but since this is for my kernel, these options are hardcoded
#
# If you want to use this script for another target, modify these variables as needed.
GCC_VERSION="15.1.0"
BINUTILS_VERSION="2.44"
TARGET="x86_64-elf"
PREFIX="$HOME/.local/gcc/x86_64-elf" # No sudo needed for this directory 
LIBGCC_CFLAGS="-mno-red-zone -O2 -fPIC"
MAKE_FLAGS=""
PROGRAM_PREFIX="x86_64-elf-"

for cmd in curl wget gpg tar make gcc g++; do
	if ! command -v "$cmd" &> /dev/null; then
		echo "Error: Required command '$cmd' not found in PATH."
		exit 1
	fi
done

while [[ $# -gt 0 ]]; do
	case "$1" in
		--gcc)
			GCC_VERSION="$2"
			shift 2
			;;
		--binutils)
			BINUTILS_VERSION="$2"
			shift 2
			;;
		--prefix)
			PREFIX="$2"
			shift 2
			;;
		--jobs)
			MAKE_FLAGS="-j$2"
			shift 2
			;;
		*)
			echo "Unknown option: $1"
			usage
			;;
	esac
done

export PATH="$PREFIX/bin:$PATH"
mkdir -p "$PREFIX"

# Get binutils and gcc and the signatures
mkdir -p cross-compiler
cd cross-compiler

# Get the signatures from the official server
curl -O "https://ftp.gnu.org/gnu/binutils/binutils-$BINUTILS_VERSION.tar.gz.sig"
curl -O "https://ftp.gnu.org/gnu/gcc/gcc-$GCC_VERSION/gcc-$GCC_VERSION.tar.gz.sig"
# Download the tarballs from the mirror, using ftpmirror requires at least one redirect
curl -L --max-redirs 1 -O "https://ftpmirror.gnu.org/binutils/binutils-$BINUTILS_VERSION.tar.gz"
curl -L --max-redirs 1 -O "https://ftpmirror.gnu.org/gcc/gcc-$GCC_VERSION/gcc-$GCC_VERSION.tar.gz"

# Now get the keyring to verify the signatures
wget https://ftp.gnu.org/gnu/gnu-keyring.gpg

# Import the keys using the temp directory, to avoid polluting the user's keyring
gpg --batch --homedir "$GNUPGHOME" --import ./gnu-keyring.gpg
gpg --batch --homedir "$GNUPGHOME" --verify binutils-$BINUTILS_VERSION.tar.gz.sig binutils-$BINUTILS_VERSION.tar.gz
gpg --batch --homedir "$GNUPGHOME" --verify gcc-$GCC_VERSION.tar.gz.sig gcc-$GCC_VERSION.tar.gz

tar xf "binutils-$BINUTILS_VERSION.tar.gz"
tar xf "gcc-$GCC_VERSION.tar.gz"

# Build binutils
mkdir -p binutils-build
cd binutils-build
../binutils-$BINUTILS_VERSION/configure --target="$TARGET" --prefix="$PREFIX" --program-prefix="$PROGRAM_PREFIX" --with-sysroot --disable-nls --disable-werror
make $MAKE_FLAGS
make install $MAKE_FLAGS

cd ..

# Build GCC
mkdir -p gcc-build
cd gcc-build
../gcc-$GCC_VERSION/configure --target="$TARGET" --prefix="$PREFIX" --program-prefix="$PROGRAM_PREFIX" --disable-nls --enable-languages=c,c++ --without-headers
make all-gcc $MAKE_FLAGS
make all-target-libgcc CFLAGS_FOR_TARGET="$LIBGCC_CFLAGS"
make install-gcc $MAKE_FLAGS
make install-target-libgcc $MAKE_FLAGS

# Add to PATH, and create a backup of .bashrc, just incase anything goes terribly wrong (unlikely)
cp ~/.bashrc ~/.bashrc.bak
echo "Created backup ~/.bashrc at ~/.bashrc.bak"
echo -e "\nexport PATH=\"$PREFIX/bin:\$PATH\"" >> ~/.bashrc

echo "Cross-compiler for $TARGET built successfully!"
