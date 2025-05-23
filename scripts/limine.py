#!/usr/bin/env python

import os
import subprocess as sp
import shutil
import sys

def do_limine_setup(script_abspath_dir: str) -> int:
    dest_dir: str = os.path.join(script_abspath_dir, "..", "tools", "limine")
    os.makedirs(dest_dir, exist_ok=True)

    # First clone the limine binary tree and build the limine utility
    url: str = "https://github.com/limine-bootloader/limine.git"
    branch: str = "v8.x-binary"
    result: sp.CompletedProcess = sp.run(f"git clone {url} --branch={branch} --depth 1",
                                         shell=True, stderr=sp.PIPE, text=True)
    if result.returncode:
        print("Failed to clone limine: {result.sderr}", file=sys.stderr)
        return 1
    result = sp.run("make -C limine", shell=True, stderr=sp.PIPE, text=True)
    if result.returncode:
        print("Failed to build limine: {result.stderr}", file=sys.stderr)
        return 1

    # Now copy over the relevant limine files
    try:
        for file in ["BOOTX64.EFI", "BOOTIA32.EFI", "limine-bios.sys", "limine-bios-cd.bin", "limine-uefi-cd.bin", "limine", "LICENSE"]:
            shutil.copy(os.path.join("limine", file), os.path.join(dest_dir, file))
    except Exception as e:
        print(f"Failed to copy over limine files: {e}")
        return 1

    # Now remove the source tree and binaries since they were copied over
    try:
        shutil.rmtree("limine")
    except Exception as e:
        print(f"Failed to remove limine tree: {e}")
        return 1

    return 0

def do_testing_setup(script_abspath_dir: str) -> int:
    testing_folder: str = os.path.join(script_abspath_dir, "..", "tools", "testing")
    src_folder: str = os.path.join(script_abspath_dir, "..", "tools", "limine")

    # Set up the folders where the ISO root is going to be
    dest_efi_dir: str = os.path.join(testing_folder, "iso", "EFI", "BOOT")
    os.makedirs(dest_efi_dir, exist_ok=True)
    dest_limine_dir: str = os.path.join(testing_folder, "iso", "boot", "limine")
    os.makedirs(dest_limine_dir, exist_ok=True)

    # Now copy over the files to the nessecary directories
    try:
        for file in ["BOOTX64.EFI", "BOOTIA32.EFI"]:
            shutil.copy(os.path.join(src_folder, file), os.path.join(dest_efi_dir, file))
        for file in ["limine-bios.sys", "limine-bios-cd.bin", "limine-uefi-cd.bin"]:
            shutil.copy(os.path.join(src_folder, file), os.path.join(dest_limine_dir, file))

        # limine.conf in the boot folder
        shutil.copy(os.path.join(src_folder, "limine.conf"), os.path.join(dest_limine_dir, "..", "limine.conf"))
    except Exception as e:
        print(f"Failed to copy limine files to the ISO root {e}")
        return 1

    return 0
