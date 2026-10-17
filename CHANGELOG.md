# Changelog

## v0.4.3

### UI / usability
- Added a clear four-step workflow at the top of the application:
  1. Browse to load a ROM
  2. Analyze the loaded ROM
  3. Choose a target save type
  4. Create the converted ROM
- Added helper text explaining the difference between Source ROM, Analyze, Target, and Output Folder.
- Kept Analyze visually separate from the Target selector.

### Repository cleanup
- Converted the project into a GitHub-friendly source tree.
- Renamed `README.txt` to `README.md`.
- Added `.gitignore`.
- Replaced version-specific text changelog files with this canonical `CHANGELOG.md`.
- Generated build artifacts and compiled executables are no longer intended to be committed.

## v0.4.2

- Browse and drag/drop now load the source ROM only.
- Analyze operates only on the already-loaded in-memory ROM.
- Analyze no longer opens a file picker.
- Target contains conversion types only.
- Output is now an output-folder workflow.
- Added Flash512 patch-aware analysis.
- Analyzer distinguishes:
  - Original library
  - Patch state
  - Effective target
- Added protection against accidental double-patching of Flash512 ROMs.
- File-load errors now include the attempted path.

## v0.4.1

- Fixed malformed generated `payload_bin.cpp` quoting.
- Removed duplicate `UNICODE` defines.
- Added generated-payload validation.

## v0.4

- Integrated Flash512 patching directly into the application.
- Removed the external `gba-flash.exe` helper dependency.

## v0.3

- Removed automatic target recommendations.
- Made conversion targets explicitly user-selected.
