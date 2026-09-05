# Rebuilding the source-based map engine

Use an isolated checkout and build directory. Do not point output at the game
installation or an active development checkout. The vanilla profile means
which pipeline runs, not proof that unpacked input files are pristine.

Requirements: Windows x64, VS2022 C++ tools, CMake 3.31.6, Python >=3.10 with
requirements.txt, .NET Core runtime, unpacked game inputs. The py launcher and
cmake need not be on PATH: tools/build_vanilla_baseline.ps1 accepts explicit
executables. The script refuses to overwrite an existing tools/config.ini.

The script runs shared art/i18n generation, hashes inputs, checks signatures,
forces vanilla data generation, configures and builds Release, then hashes
again. It does not launch or inject into the game. Keep build outputs and
manifests local; do not distribute restricted data or the staged Oodle library.

Example (PowerShell):

    ./tools/build_vanilla_baseline.ps1 -Python <python.exe> -CMake <cmake.exe> -GameDir <unpacked-game> -BuildDir <isolated-build>

The wrapper stages an ignored Oodle copy in tools/ because the bundled
SoulsFormats probes the working directory. Merely setting config.game_dir
does not satisfy its native-library detection. Shared art is generated entirely
from committed inputs; it does not require external game gfx.

baseline_manifest.py uses content hashes, not size/mtime, and exits nonzero
for missing required input groups. Compare the input portions of before/after
manifests to detect changes during the build. Compare generated across two
forced builds for deterministic outputs. No timestamp or machine-specific root
is included. The manifest deliberately does not substitute committed ERR data
for absent vanilla outputs.

## First measured baseline, 2026-09-05

Public source a25443312dd07c21bb616bd2aeda16ee889df045.

- Shared generators completed: 65 map icons; localization bundles validated.
- Installed executable: 29 upstream signature checks; 28 unique, including every
  critical signature. Noncritical map_wmd_dtor_hook missing.
- Source signature drift guard passed.
- VS2022 BuildTools and CMake 3.31.6 available.
- Initial vanilla extraction loaded 194 paramdefs, decoded regulation and FMG,
  and produced 196 conversion rows / 1,007 valid location IDs.
- Input provenance is not attested as pristine vanilla; installed game includes
  mod-related folders. Hashes identify bytes, not provenance.
- Signature success is not evidence of safe layouts, lifecycle, rendering or
  game-version support. No in-game baseline has been exercised.

All seven CMake Git dependencies are pinned to the exact commits used by the
first successful Windows compile. Python dependencies and toolchain versions
are recorded in the local build evidence; this is not yet a hermetic release
build.

The first scan exposed a source bug: refresh hooks stayed active when the close
hook failed. Fast-map optimization now stays pass-through until every required
hook is installed; otherwise deferred marker pointers could outlive map close.
This disables optimization on this executable until the destructor is ported.
MSB extraction completed: 1,347 files, zero parse errors, 26,778 item records.
Full pipeline/build and in-game acceptance remain separate gates.

CI pins Windows 2022 and CMake 3.31.6, matching the tested build generation.
The legacy MinHook CMake project fails configuration under CMake 4 defaults;
newer CMake/toolset support requires a separate dependency compatibility check.
