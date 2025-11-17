Build
=====
This project uses Kconfig to configure the kernel, and uses Linux's menuconfig tool.

You do need to clone the submodules in order to build this project. To do this, you can run this command: `git submodule update --init --recursive`. If you haven't cloned the project yet,
you can run `git clone <url> --recurse-submodules`

You can only build this project with a GCC cross compiler (x86_64-elf-gcc). The build script is located in another repository [here](https://github.com/primeseven1/lunar-user). The script is located at scripts/gcc.sh

To configure the project, run `make menuconfig`, and then you can configure the kernel how you would like.

After that, run `make` to build the kernel.
