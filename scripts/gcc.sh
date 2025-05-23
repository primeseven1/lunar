#!/usr/bin/env bash

set -e

build_gcc() {
	local GCC_VERSION="$1"
	local BINUTILS_VERSION="$2"
	local TARGET="$3"
	local PREFIX="$4"
	local LIBGCC_CFLAGS="$5"
	local MAKE_FLAGS="$6"
	local PROGRAM_PREFIX="$7"

	export PATH="$PREFIX/bin:$PATH"
	mkdir -p "$PREFIX"

	mkdir -p cross-compiler
	cd cross-compiler
	curl -O "https://ftp.gnu.org/gnu/binutils/binutils-$BINUTILS_VERSION.tar.gz"
	curl -O "https://ftp.gnu.org/gnu/gcc/gcc-$GCC_VERSION/gcc-$GCC_VERSION.tar.gz"
	tar xf "binutils-$BINUTILS_VERSION.tar.gz"
	tar xf "gcc-$GCC_VERSION.tar.gz"

	mkdir -p binutils-build
	cd binutils-build
	../binutils-$BINUTILS_VERSION/configure --target="$TARGET" --prefix="$PREFIX" --program-prefix="$PROGRAM_PREFIX" --with-sysroot --disable-nls --disable-werror
	make $MAKE_FLAGS
	make install $MAKE_FLAGS

	cd ..

	mkdir -p gcc-build
	cd gcc-build
	../gcc-$GCC_VERSION/configure --target="$TARGET" --prefix="$PREFIX" --program-prefix="$PROGRAM_PREFIX" --disable-nls --enable-languages=c,c++ --without-headers
	make all-gcc $MAKE_FLAGS
	make all-target-libgcc CFLAGS_FOR_TARGET="$LIBGCC_CFLAGS"
	make install-gcc $MAKE_FLAGS
	make install-target-libgcc $MAKE_FLAGS

	cp ~/.bashrc ~/.bashrc.backup
	echo "Created backup ~/.bashrc at ~/.bashrc.backup"
	echo -e "\nexport PATH=\"$PREFIX/bin:\$PATH\"" >> ~/.bashrc

	cd ../..
	rm -rf ./cross-compiler

	echo "Cross-compiler for $TARGET built successfully!"
}

NPROC=$(nproc)
if [[ $NPROC -ge 2 ]]; then
	NPROC=$(( $NPROC / 2 ))
fi

build_gcc "15.1.0" "2.44" "x86_64-elf" "$HOME/.toolchains/gcc/x86_64-elf" "-O2 -mno-red-zone" "-j$NPROC" "x86_64-elf-"
