GCC Script
==========

This script is located in scripts/gcc.sh.

Command Line Options
--------------------
--jobs: Equivalent to the -j option in Make. Usage: --jobs <JOBCOUNT>
--gcc: The GCC version to use. Usage: --gcc <GCCVERSION>
--binutils: The binutils version to use. Usage: --binutils <BINUTILSVERSION>
--prefix: The directory the toolchain will be installed in. Usage: --prefix <DIRECTORY>

How it Works
------------
This script works by grabbing the GPG signatures from the official GNU ftp server, and then grabs the actual source code from a mirror provided on GNU's website for faster speeds.

Then a temp directory is created for the key imports to ensure your keyrings are not modified. The temp directory is listed once the script exits.

After that, the script verifies the signatures, and then extracts the source code from the tarballs and builds it.
