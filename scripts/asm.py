#!/usr/bin/env python

import sys
import os
import subprocess

def load_flags(abspd: str) -> str:
    compile_flags_path: str = os.path.join(abspd, "..", "compile_flags.txt")
    if not os.path.isfile(compile_flags_path):
        return ""
    with open(compile_flags_path, "r") as cf:
        flags: list[str] = []
        ignored_flags: list[str] = ["-c", "-MMD", "-MP"]
        for line in cf:
            line = line.strip()
            if line and line not in ignored_flags:
                flags.append(line)
        return " ".join(flags)

def main() -> int:
    argv: list[str] = sys.argv
    if len(argv) < 2:
        print(f"Usage: {argv[0]} <filepath> <optimization>", file=sys.stderr)
        return 1

    pager: str = "less"
    compiler: str = "x86_64-elf-gcc"
    file: str = os.path.abspath(argv[1])
    opt: str = argv[2] if len(argv) > 2 else "-O0"

    abspd: str = os.path.dirname(os.path.abspath(__file__))
    flags: str = load_flags(abspd)

    gcc_cmd: str = f"{compiler} -S {file} {flags} {opt} -o -"
    try:
        cwd: str = os.path.abspath(os.path.join(abspd, ".."))
        gcc_sp = subprocess.run(gcc_cmd, stdout=subprocess.PIPE, check=True,
                                stderr=subprocess.DEVNULL, cwd=cwd, shell=True)
        subprocess.run([pager], input=gcc_sp.stdout, check=True)
    except subprocess.CalledProcessError as e:
        print(f"\033[31mCommand failed:\033[0m {e}\n\033[33mIs the kernel configured?\033[0m",
              file=sys.stderr)
        return 1

    return 0

if __name__ == "__main__":
    sys.exit(main())
