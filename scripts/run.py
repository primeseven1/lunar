#!/usr/bin/env python

import argparse as ap
import os
import subprocess as sp
import sys
import limine

def create_cmdline_parser() -> ap.ArgumentParser:
    parser: ap.ArgumentParser = ap.ArgumentParser()
    parser.add_argument("--cpus", "-c", type=int, default=1,
                        help="The number of logical cores the VM should have")
    parser.add_argument("--debug", "-d", action="store_true",
                        help="Add flags that allow attaching a debugger")
    parser.add_argument("--efi", "-e", action="store_true",
                        help="Start qemu with OVMF instead of seabios")
    parser.add_argument("--ovmf-path", "-o", type=str,
                        default=os.path.join(os.sep, "usr", "share", "edk2", "x64"),
                        help="Folder with the OVMF files")
    parser.add_argument("--memory", "-m", type=str, default="128M",
                        help="The amount of memory the VM should have")

    parser.add_argument("--setup", "-s", action="store_true",
                        help="Setup ISO root")
    parser.add_argument("--limine", "-l", action="store_true",
                        help="Meant to be used with --setup to clone the bootloader")
    parser.add_argument("--kvm", "-k", action="store_true",
                        help="Enable KVM")
    return parser

def parse_cmdline_args(iso_file: str) -> str:
    qemu_args: str = f"-cdrom {iso_file} -net none -debugcon stdio -machine q35"

    parser: ap.ArgumentParser = create_cmdline_parser()
    args = parser.parse_args()

    if args.debug:
        qemu_args += " -S -s"
    if args.efi:
        ovmf_code = os.path.join(args.ovmf_path, "OVMF_CODE.4m.fd")
        ovmf_vars = os.path.join(args.ovmf_path, "OVMF_VARS.4m.fd")
        qemu_args += f" -drive if=pflash,unit=0,file={ovmf_code},readonly=on"
        qemu_args += f" -drive if=pflash,unit=1,file={ovmf_vars},readonly=on"
    qemu_args += f" -m {args.memory}"
    qemu_args += f" -smp cpus={args.cpus}"
    if args.kvm:
        qemu_args += " -enable-kvm -cpu host,migratable=no,+invtsc"

    return qemu_args

def run_x86_64(qemu_args: str) -> int:
    ret: sp.CompletedProcess = sp.run(f"qemu-system-x86_64 {qemu_args}", shell=True,
                                      stderr=sp.PIPE, text=True)
    if ret.returncode:
        print(f"Error when running qemu: {ret.stderr}", file=sys.stderr)
    return ret.returncode

def main() -> int:
    script_abspath_dir: str = os.path.dirname(os.path.abspath(__file__))
    if "--setup" in sys.argv:
        if "--limine" in sys.argv:
            limine.do_limine_setup(script_abspath_dir)
        limine.do_testing_setup(script_abspath_dir)
        return 0

    iso: str = os.path.join(script_abspath_dir, "..", "tools", "testing", "crescent.iso")
    qemu_args: str = parse_cmdline_args(iso)
    return run_x86_64(qemu_args)

if __name__ == "__main__":
    sys.exit(main())
