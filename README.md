# GBA Repro Save Patcher

A Windows utility for analyzing Game Boy Advance ROM save libraries and converting supported ROMs for different repro-cartridge save hardware.

Current version: **v0.4.3**

## Workflow

The app is intentionally linear:

1. **Browse** to load the source `.gba` ROM.
2. Click **Analyze loaded ROM**.
3. Choose the desired **Target** save type.
4. Choose an **Output Folder**.
5. Click **Create** to generate the converted ROM.

The ROM selected by **Browse** (or drag-and-drop) is loaded into memory and becomes the single source of truth for analysis and conversion. **Analyze never opens another file picker.**

## Current conversion targets

- **SRAM**
- **Flash512**

The program does not guess which cartridge board you own and does not automatically recommend a target. The user chooses the save-hardware target.

Native **FLASH / FLASH1M** conversion is intentionally not enabled yet.

## Patch-aware analysis

The analyzer reports three separate concepts:

- **Original library**
- **Patch state**
- **Effective target**

For example, a patched Minish Cap ROM may still contain the text `EEPROM_V124`, while the analyzer correctly reports:

```text
Original library: EEPROM_V124
Patch state:      Flash512 payload + hooks detected
Effective target: Flash512
```

That is expected because the original SDK save-library string can remain in the ROM after the actual save routines have been redirected.

## Flash512 support

Flash512 support is built directly into the application. No separate `gba-flash.exe` helper is required.

At build time:

```text
third_party/gba-flash-patcher/payload.c
    -> ARM7TDMI payload
    -> raw binary
    -> generated C++ byte array
    -> linked into GBA-Repro-Save-Patcher.exe
```

At runtime, supported ROMs are patched in-process.

## Supported EEPROM libraries

The built-in EEPROM-to-SRAM stage currently supports:

- `EEPROM_V120`
- `EEPROM_V121`
- `EEPROM_V122`
- `EEPROM_V124`
- `EEPROM_V126`

The Legend of Zelda: The Minish Cap commonly uses `EEPROM_V124`.

## Build requirements

The current build scripts target Windows using MSYS2 UCRT64.

Install the required toolchains from an MSYS2 UCRT64 shell:

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc
pacman -S mingw-w64-ucrt-x86_64-arm-none-eabi-toolchain
```

Then run:

```text
BUILD_PORTABLE.bat
```

This creates:

```text
GBA-Repro-Save-Patcher.exe
```

## Install

Run:

```text
INSTALL.bat
```

The installer:

- builds the application if needed
- installs it for the current Windows user
- creates Start Menu and Desktop shortcuts
- does not require administrator rights

## Portable use

Run:

```text
BUILD_PORTABLE.bat
```

and then launch:

```text
GBA-Repro-Save-Patcher.exe
```

## Repository layout

```text
GBA-Repro-Save-Patcher/
├── README.md
├── LICENSE.txt
├── THIRD_PARTY_NOTICES.txt
├── CHANGELOG.md
├── .gitignore
├── BUILD_PORTABLE.bat
├── INSTALL.bat
├── RUN_PORTABLE.bat
├── UNINSTALL.bat
├── src/
│   ├── main.cpp
│   ├── app.rc
│   ├── app.ico
│   ├── app.manifest
│   └── embed_payload.ps1
└── third_party/
    └── gba-flash-patcher/
        ├── payload.c
        ├── payload.ld
        └── LICENSE_UPSTREAM.txt
```

## Generated build files

The build process temporarily creates files such as:

- `src/payload.elf`
- `src/payload.bin`
- `src/payload_bin.cpp`
- `src/app_res.o`

These are generated artifacts and are excluded by `.gitignore`.

Compiled `.exe` files are also ignored in the source tree. Release builds are better distributed through GitHub Releases.

## Source ROM safety

The loaded source ROM is never overwritten. Converted ROMs are written as new files to the selected output folder.

## Credits

EEPROM conversion patterns are adapted from:

- **BBsan**, `Flash1M_Repro_SRAM_Patcher` (MIT)

Flash512 payload/reference implementation:

- **Metroid Maniac**, `gba-flash-patcher` (MIT)

See `THIRD_PARTY_NOTICES.txt` and the bundled upstream license under `third_party/`.
