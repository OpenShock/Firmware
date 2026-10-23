# Developer guide

Everything needed to build, flash, test and maintain the firmware locally. CI runs the same
scripts, so what works here works there.

## Toolchain

| Tool | Version | Notes |
|---|---|---|
| ESP-IDF | v6.1 | The version CI pins (`.github/actions/setup-esp-idf`). Install with the Espressif installer (EIM); the scripts and CMake presets find that install on their own. |
| Python | 3.13 (`.python-version`) | For the repository scripts. ESP-IDF brings its own venv. |
| Node | 26 (`frontend/.nvmrc`) | Captive-portal frontend. |
| pnpm | 12 (`packageManager` in `frontend/package.json`) | `corepack enable` installs it. |
| flatc | matches the flatbuffers submodule | Only when editing schemas; `scripts/fetch_flatc.py` downloads it. |

Clone with submodules. The flatbuffers runtime is a submodule and the build fails without it:

```sh
git clone --recurse-submodules https://github.com/OpenShock/Firmware.git
# or, in an existing clone
git submodule update --init
```

### Activate ESP-IDF

`scripts/build.py` finds an ESP-IDF installed by the Espressif installer (EIM) on its own: it reads
the installer's `eim_idf.json` and applies that install's activation environment before running
`idf.py`. So the build script, and the VS Code tasks that call it, work from a plain terminal.

Direct `idf.py` calls, or an ESP-IDF installed some other way, still need the environment in the
shell. The activation only lasts for that shell. Two helpers do the same discovery for a shell:

```sh
. scripts/idfenv.sh          # bash, zsh, sh on Linux and macOS
call scripts\idfenv.bat      # cmd.exe
```

On Windows use PowerShell or cmd. ESP-IDF refuses MSYS shells such as Git Bash, and the
installer's environment is in Windows path form, which those shells cannot use.

Windows, PowerShell. The installer writes a profile under the tools directory. Use it rather than
the generic `export.ps1`, which looks for a venv under your home folder and fails:

```powershell
. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1
```

Linux and macOS:

```sh
. ~/esp/v6.1/esp-idf/export.sh      # path depends on where you installed it
```

### Windows: UTF-8 console

ESP-IDF prints non-ASCII glyphs. Python on Windows encodes console output with the legacy code
page unless UTF-8 mode is on, and crashes with `UnicodeEncodeError` otherwise. `scripts/build.py`
sets `PYTHONUTF8=1` for the `idf.py` it launches. For direct `idf.py` calls, set it once for your
user:

```powershell
[Environment]::SetEnvironmentVariable("PYTHONUTF8", "1", "User")
```

### Python dependencies

The scripts need a few packages beyond what ESP-IDF's venv ships. Install them into a venv of
your own, not into ESP-IDF's: `requirements.txt` pins a newer `cryptography` than ESP-IDF's
constraints allow, and installing it there breaks the ESP-IDF activation.

```sh
python -m venv .venv && . .venv/bin/activate      # Windows: .venv\Scripts\activate
pip install --require-hashes -r requirements.txt
```

The one exception is the `staticfs` build target, which ESP-IDF's own interpreter runs: it needs
`littlefs-python` in the ESP-IDF venv. Install just that one there, at the version pinned in
`requirements.in`.

## Building

### Build a board

`scripts/build.py` is the build driver. It applies the board's defaults on top of
`sdkconfig.defaults`, builds into `build/<board>/`, and stamps the board descriptor into the app
image after linking. Anything after the board name is passed to `idf.py`.

```sh
python scripts/build.py OpenShock-Core-V2                    # full build, incl. staticfs
python scripts/build.py OpenShock-Core-V2 app                # app only
python scripts/build.py OpenShock-Core-V2 flash -p COM4      # build and flash
python scripts/build.py OpenShock-Core-V2 monitor -p COM4    # serial monitor
python scripts/build.py OpenShock-Core-V2 fullclean          # wipe build/<board>
python scripts/build.py OpenShock-Core-V2 menuconfig         # tweak Kconfig for this build dir
```

Board names are the file names under `boards/` without `.defaults`.

`OPENSHOCK_BUILD_DIR` overrides the build directory.

### Build a variant (what CI does)

Boards on the same chip with the same Kconfig compile identical objects. CI compiles once per
variant and stamps each board into a copy afterwards:

```sh
python scripts/build.py --variant esp32s3-6832dbd5 app bootloader partition-table
python scripts/build.py --variant ?                  # any unknown id lists the variants and their boards
```

### How a board is defined

`boards/<board>.defaults` holds two kinds of lines:

- `CONFIG_*` Kconfig lines: chip target, flash size, console, PSRAM. Applied on top of
  `sdkconfig.defaults` at configure time.
- `OPENSHOCK_*` pin lines: not Kconfig. Stripped from the fragment and stamped into the image by
  `scripts/stamp_board.py`, so the same compiled objects serve every board of a chip.

Shared config lives in `sdkconfig.defaults`, including the 4 MB OTA partition table
(`partitions/ota_4mb.csv`) every board uses. `sdkconfig` and `build/` are generated and
gitignored. If a `sdkconfig` is stale after a defaults change, delete it or run `fullclean`.

```sh
python scripts/stamp_board.py read --image build/OpenShock-Core-V2/firmware.bin
```

### VS Code

The repository carries its own VS Code setup in `.vscode/`. Open the folder, install the
recommended extensions when prompted, and:

- **Tasks** (Terminal → Run Task, or the default build task): build, build app only, flash,
  monitor, menuconfig, full clean and update managed components, each asking for a board and,
  where relevant, a serial port. They call `scripts/build.py`, which activates ESP-IDF itself.
- **IntelliSense** comes from clangd via `.clangd`, which reads `build/compile_commands.json`.
  The build script refreshes that file from whichever board or variant it built last, so build a
  board once and clangd follows. Stock clangd does not know the Xtensa targets; install
  Espressif's esp-clangd (the installer puts it under the tools directory, e.g.
  `C:\Espressif\tools\esp-clangd\<version>\esp-clangd\bin\clangd.exe`) and point `clangd.path`
  at it in your **user** settings, since the path is machine specific.
- `launch.json` and `c_cpp_properties.json` are not tracked; a debugger setup with OpenOCD is
  per machine.

### CLion, or plain CMake

`CMakePresets.json` has a configure preset and a build preset per board. A preset passes only the
board name; the root `CMakeLists.txt` derives the chip and the sdkconfig layers from the board
file and activates an installer-registered ESP-IDF itself, so no exported shell is needed:

```sh
cmake --preset OpenShock-Core-V2
cmake --build --preset OpenShock-Core-V2                      # everything, incl. staticfs
cmake --build --preset OpenShock-Core-V2 --target app
ESPPORT=COM4 cmake --build --preset OpenShock-Core-V2 --target flash
ESPPORT=COM4 cmake --build --preset OpenShock-Core-V2 --target monitor
```

The presets build into `build/<board>/`, the same directories `scripts/build.py` uses, so the
two can be mixed. Flash and monitor read the port from `ESPPORT`; `menuconfig` is a target too.
A preset build stamps the board like the script does, since the stamp is a CMake post-build step.
The file also has a `default` preset with no board. It exists for `idf.py`, which configures with
the preset of that name once a `CMakePresets.json` is present, and it is what `scripts/build.py`
runs under; the script's own `-B` and `-D` arguments take precedence, so it changes nothing.
Differences from the script: ccache is off (idf.py turns it on), and the bootloader's nested
configure prints a warning about `ESP_ROM_ELF_DIR` when the shell is not exported; it is harmless.

#### First-time setup in CLion

1. **Open the repository root** (File → Open, pick the folder with `CMakeLists.txt`). Choose
   "Open as CMake project" if asked. CLion finds `CMakePresets.json` and shows the boards as
   disabled profiles; it may also create a "Debug" profile of its own. Delete that one, since it
   has no board and will fail to configure.
2. **Enable the boards you build** under Settings → Build, Execution, Deployment → CMake. Each
   preset is a profile; tick "Enable profile" on the ones you want. Leave the toolchain at the
   default. The preset supplies the generator, and CMake caches the compiler paths from the
   ESP-IDF install, so CLion's own toolchain is not used for compiling.
3. **Let the first configure run.** It takes about half a minute per profile and prints
   `ESP-IDF activated from ...` early in the CMake tool window. If it instead stops with
   "no ESP-IDF install was found", ESP-IDF was not installed with the Espressif installer: add the
   environment to the toolchain (Settings → Build, Execution, Deployment → Toolchains, "Add
   environment" → "From file") pointing at `scripts/idfenv.bat` on Windows or `scripts/idfenv.sh`
   elsewhere, or run CLion from a shell where the ESP-IDF export script has been sourced.
4. **Pick a target.** The run/build configuration dropdown lists the CMake targets of the active
   profile: `app` compiles the firmware, `flash` writes it, `monitor` opens the serial console,
   `menuconfig` edits the configuration. For `flash` and `monitor` set `ESPPORT` (e.g. `COM4`) in
   the configuration's environment variables.
5. **Build with Ctrl+F9** or the hammer icon. Output lands in `build/<board>/`, shared with
   `scripts/build.py`.

Code insight comes from CLion's own CMake integration, so no clangd setup is needed. The
`.idea/` directory and `CMakeUserPresets.json` are ignored, so machine-specific profiles and
settings stay local. The presets file is generated from `boards/`; after adding a board run:

```sh
python scripts/gen_cmake_presets.py
```

### Captive-portal frontend and staticfs

The web UI in `frontend/` is built with Vite, gzipped, and packed into a littlefs image for the
`static0` partition. The CMake `staticfs` target does this automatically during a full build and
only rebuilds the frontend when its sources changed. To work on the frontend itself:

```sh
cd frontend
pnpm install
pnpm run dev        # dev server
pnpm run check      # svelte-check, what CI runs
pnpm run lint       # prettier + eslint
pnpm test:unit
```

To build only the image, as CI does:

```sh
python scripts/gen_staticfs.py --frontend-dir frontend --staging-dir build/staticfs \
    --output build/staticfs.bin --partition-csv partitions/ota_4mb.csv
```

### Packaged and merged images

Releases ship one binary per board, flashable at offset 0. CI gets there in three steps, which
run locally too. `package_boards.py` expects each variant directory to hold `app.bin`,
`bootloader.bin` and `partition-table.bin`, the names CI uploads; a local variant build writes
`firmware.bin`, `bootloader/bootloader.bin` and `partition_table/partition-table.bin`, so copy
them first.

```sh
python scripts/build.py --variant esp32s3-6832dbd5 app bootloader partition-table
mkdir -p out/variants/esp32s3-6832dbd5
cp build/esp32s3-6832dbd5/firmware.bin                        out/variants/esp32s3-6832dbd5/app.bin
cp build/esp32s3-6832dbd5/bootloader/bootloader.bin           out/variants/esp32s3-6832dbd5/
cp build/esp32s3-6832dbd5/partition_table/partition-table.bin out/variants/esp32s3-6832dbd5/

python scripts/package_boards.py --variants out/variants --board "OpenShock-Core-V2" --out out/boards
python scripts/merge_image.py --board OpenShock-Core-V2 --bindir 'out/boards/{board}' \
    --staticfs build/staticfs.bin --output 'out/merged/{board}.bin'
esptool write_flash 0x0 out/merged/OpenShock-Core-V2.bin
```

`merge_image.py` needs only esptool, not ESP-IDF.

## Testing

### Host tests (C++)

Each `components/<name>/host_test/` is its own ESP-IDF project for the `linux` target with a Unity
suite. The ELF exits with the number of failures. Linux only; needs `libbsd-dev` and Ruby (ESP-IDF
generates CMock mocks with it).

```sh
idf.py -C components/config/host_test --preview set-target linux build
components/config/host_test/build/*_host_test.elf
```

The loop CI runs over all of them is in `.github/workflows/host-tests.yml`.

### Script tests (Python)

```sh
pip install --require-hashes -r requirements.txt -r .github/scripts/requirements-dev.txt
ruff check .
mypy
python -m pytest
```

Config for all three is in `pyproject.toml`.

### Lint

C++ formatting is `.clang-format`, enforced by the cpp-linter workflow. Run
`clang-format -i` on changed files before pushing. clang-tidy is configured but not run in CI.

## Updating dependencies

### ESP-IDF managed components

Components declare registry dependencies in their own `idf_component.yml` (today `components/core`
and `components/osjson`); `dependencies.lock` at the root pins them and is committed.
Update through the board script so the lock is solved for the same target CI uses:

```sh
python scripts/build.py OpenShock-Core-V2 update-dependencies
```

Commit the resulting `dependencies.lock`. A plain `idf.py update-dependencies` works too but
also flips the lock's `target` line to whatever the plain sdkconfig targets.

### ESP-IDF itself

Change the `idf-version` default in `.github/actions/setup-esp-idf/action.yml`, install the same
version locally, rebuild every variant, and run the host tests. Expect Kconfig renames; a
reconfigure prints what it ignored.

### Python

`requirements.txt` is compiled from `requirements.in` with hashes:

```sh
uv pip compile requirements.in --universal --python-version 3.10 --generate-hashes -o requirements.txt
```

The CI helper scripts have their own pinned file; the refresh command is in the header of
`.github/scripts/requirements.txt`.

### Frontend

```sh
cd frontend
pnpm update --latest    # or edit package.json
pnpm install
pnpm run check && pnpm run lint
```

Commit `package.json` and `pnpm-lock.yaml`. Dependabot also opens bumps for these.

### FlatBuffers schemas

`schemas/` is a submodule of the shared `flatbuffers-schemas` repository. Generated code is
committed in two places and must match the vendored flatbuffers runtime exactly:

```sh
git -C schemas pull origin <branch>          # move the schema submodule
python scripts/fetch_flatc.py                # flatc matching components/flatbuffers/flatbuffers
python scripts/generate_schemas.py           # regenerates C++ and TypeScript
```

To move the flatbuffers runtime itself to a newer release, run `fetch_flatc.py --upgrade`. It
advances the submodule and fetches the matching flatc; then regenerate.

### TLS trust store

`certificates/cacert-merged.pem` is compiled into the firmware. Regenerating it requires a manual
checksum check against curl's site. Follow `certificates/README.md` exactly.

## Useful extras

- Component dependency graph: `python scripts/gen_dep_graph.py` emits Mermaid.
- `build/compile_commands.json` is the compile database of the last build; `.clangd` points at it.
- CHANGELOG.md and RELEASE.md hold release notes and the flashing instructions shipped with a
  release.
