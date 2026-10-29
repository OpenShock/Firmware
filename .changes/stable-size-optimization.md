---
kind: changed
---
Compile stable releases for size, without assert messages

## Release Note
Smaller firmware

Stable releases are compiled for size instead of for debugging, which leaves more room for future updates. The firmware stays slightly smaller than 1.5.2, where a debug-compiled build would have been 9 to 14% larger. Develop and beta builds are unchanged.

- Stable CI builds (master, final tags and pull requests into master) select `CONFIG_COMPILER_OPTIMIZATION_SIZE` and `CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_SILENT`; `OPENSHOCK_FW_OPTIMIZATION=size|debug` overrides the choice for a local build, which then gets its own build directory
- Each release attaches one ELF per build variant (`OpenShock_<variant>_<version>.elf`), which decodes the backtrace of a failed assert now that stable builds print no assert message
- `gen_env_header.py` resolves the git commit from the repository, so a build directory outside it no longer fails on an incomplete `openshock_version.h`
