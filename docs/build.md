Build
=====
This project uses Kconfig to configure the kernel, and uses Linux's menuconfig tool.

You do need to clone the submodules in order to build this project. To do this, you can run this command: `git submodule update --init --recursive`. If you haven't cloned the project yet,
you can run `git clone url --recurse-submodules`

You can build this project with clang and gcc (x86_64-elf-gcc/x86_64-elf-ld). There is a script to install a gcc cross compiler in the scripts folder, which requires no arguments.
The script requires no arguments, and feel free to make a copy of the script and modify the options passed to build_gcc.

To configure the project, run `make menuconfig`, and then you can configure the kernel how you would like.

After that, run `make` to build the kernel.

Running
=======
First, install QEMU

Now run the run.py file in the scripts folder with this command: `./run.py --setup --limine`. If you update the limine.conf file in tools/limine/limine.conf, then you only need to run the script with the --setup option and rebuild the ISO.

After that, run `make iso` in project root, and then run the run.py script again without any arguments, and then QEMU should run. Run the script with the --help option to see more arguments you can use.
