Version Checker
===============
This script contains a function that contains the logic for checking to make sure the user has the minimum version of a tool.

Usage
=====
```sh
source /path/to/ver.sh

# tool - The name of the tool (eg. gcc)
# version_str - The version string (major.minor)
# min_major - The minimum major version for the tool
# min_minor - The minimum minor version for the tool
check_version tool version_str
```

On failure, the function will return a non-zero value
