# Crescent
A 64 bit operating system kernel built from scratch

This project is in **early development**. Expect bugs and rapid changes.

# Goals
* File system support
* POSIX compatibility
* User space

# Building
Setup and build instructions are documented [here](docs/build.md)

# Real Hardware
Important: This kernel interacts with low-level hardware and could cause irreversible damage in some situations. The project is experimental and under active development. If you plan to run this on real hardware, proceed carefully --- **run at your own risk**.

If you choose to write the ISO to a USB drive or other block device, we've included a helper script: scripts/blkdev.sh. The script performs some safety checks (for example, it looks for mounted partitions), but these checks are not foolproof and cannot guarantee safety. **Use the script at your own risk** --- double-check the target device before proceeding.
