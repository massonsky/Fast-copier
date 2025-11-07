# Fast Copier Tooling Setup

High-performance CLI copier project scaffolded around CMake with dual dependency managers (Conan & vcpkg) and embedded Git build metadata.

## Prerequisites

- CMake 3.21+
- Ninja or Visual Studio/MSBuild on Windows; any CMake-supported generator elsewhere
- Git (for metadata extraction)
- Python 3 with Conan 2.x (`pip install conan`)
- vcpkg clone (set `VCPKG_ROOT` environment variable)

## One-Time Environment Bootstrap

1. **Install Conan dependencies**

   ```powershell
   scripts\setup_conan.ps1
   ```

   Linux/macOS equivalent:

   ```bash
   ./scripts/setup_conan.sh
   ```

   This detects or creates the default Conan profile and runs `conan install` for Debug/Release into `build/conan/`.

2. **Install vcpkg dependencies (optional alternative)**

   ```powershell
   scripts\setup_vcpkg.ps1
   ```

   Pass `--triplet <name>` to override the default (`x64-windows` on Windows, `x64-linux` elsewhere).

## Configuring with CMake Presets

The repository ships with `CMakePresets.json` that exposes four presets:

- `conan-debug`, `conan-release`
- `vcpkg-debug`, `vcpkg-release`

Select a preset from VS Code (CMake Tools) or run manually:

```powershell
cmake --preset conan-debug
```


## Building Targets


```powershell
cmake --build --preset conan-debug --target fcopyrover
```

`fcopyrover` links against the `cclone` interface library which carries shared include paths and dependencies (fmt, spdlog, CLI11).

## Running Tests

After configuring with a preset, build and execute tests:

```powershell
cmake --build --preset conan-debug --target cclone_tests
ctest --preset conan-debug -C Debug --output-on-failure
```

(Replace `conan-debug` with another preset as desired.)

## Embedded Git Metadata

During configure, CMake captures Git branch, commit, tag, dirty state, and UTC build timestamp via `cmake/git_info.hpp.in`. The generated header is emitted to `build/<preset>/generated/internal/git_info.hpp` and exposed through the `cclone` interface so code can `#include <internal/git_info.hpp>` and access `cclone::build_info::*` constants.

## Adding New Components

- Place new library headers/sources under `src/` in their respective subsystem directories.
- Update `CMakeLists.txt` with additional sources or targets; shared logic should link to `cclone`.
- Tests belong in `tests/` and should be registered via GoogleTest (already bundled via Conan/vcpkg or automatic FetchContent fallback).

## Regenerating Dependencies

Whenever `conanfile.py` or `vcpkg.json` changes, rerun the respective setup script or manual install command to refresh the dependency graph.

## VS Code Integration

- Enable *CMake Tools* preset support (`"cmake.useCMakePresets": true`).
- Ensure `VCPKG_ROOT` is configured in **CMake: Configure Environment** if using vcpkg presets.
- Trigger configure from the status bar or command palette; subsequent build/debug actions follow the active preset.
