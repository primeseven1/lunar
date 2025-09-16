#!/usr/bin/env bash

set -e

if [[ ! -d tools/menuconfig ]]; then
	git clone https://github.com/anatol/menuconfig.git tools/menuconfig --depth=1
fi

ninja -C ./tools/menuconfig
./tools/menuconfig/mconf Kconfig

rm -f ./include/generated/rustc_cfg
rm -rf ./include/config
