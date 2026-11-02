# Native Windows build (verified playable, PAL `GGSPA4`)

This is the recipe that produced a playable native Windows run of the
PAL build on 2026-11-02: boot -> Konami logo -> intro FMV -> title ->
`NEW GAME` -> codec -> opening cutscene -> playable Dock area, presented
in a window via SDL3/Vulkan. It uses upstream's LLVM native backend
(the mainline), not the C backend.

Everything below is driven from `tools/windows/*.bat`. Paths are
environment variables, not constants: earlier local scripts hardcoded
drive letters and are not needed.

## Toolchain

| Piece | What works | What does not |
|---|---|---|
| Host (`twin-snakes.exe`) | Qt MinGW GCC 13.1, CMake + Ninja | MSVC - the tree uses GCC-style atomics/pthread idioms |
| DolRecomp (LLVM) | MSVC (VS 18) + LLVM 20 dev tree | MinGW headers (too old for the Dolphin-derived parts) |
| Module link | clang + **lld** (llvm-mingw) | GNU `ld` - links for a long time, then produces a corrupt DLL (Windows load error 193) |
| Shaders | prebuilt SPIR-V via glslang, passed as `MGS_PREBUILT_SHADERS` | a bare checkout with no `glslc` - the GPU test then fails |

LLVM dev tree: the official Windows installer ships no
`LLVMConfig.cmake` and no static libraries. Extract `include/`, `lib/`
and `bin/` from the `clang+llvm-*-x86_64-pc-windows-msvc.tar.xz`
release tarball and point `LLVM_DIR` at its `lib/cmake/llvm`.

## Steps

1. **Discs.** Extract your own PAL discs to `discs/GGSPA4/disc1` and
   `discs/GGSPA4/disc2` (each: `sys/` + `files/`, including
   `sys/fst.bin`). The stdlib-only extractor in `tools/` does this with
   no wit/Dolphin install. Verify against `config/GGSPA4.toml`
   (`main.dol`, `mgso_pal.rel`, apploader SHA-1s must match).

2. **Dependencies.** `tools/bootstrap.sh` fetches `extern/` and applies
   `tools/patches/*.patch`. On Windows, keep the patch files LF
   (`.gitattributes` pins them): CRLF patch files make `git apply`
   fail inside `extern/` and bootstrap skips them with only a
   "does not fit" note. `git config core.longpaths true` is needed for
   the ModernGekko template's SPIRV-Cross submodule.

3. **Host.** Set `MGS_QT_TOOLS`, `SDL3_DIR`, `MGS_PREBUILT_SHADERS`
   (if needed), then `tools\windows\build-host.bat`.
   Run tests **from the repo root** - the disc tests use relative
   paths and fail misleadingly from inside the build dir - with the
   MinGW `bin` and SDL3 `bin` directories on `PATH` (the test
   executables load SDL3.dll):
   `ctest --test-dir build-mingw` -> 22/22.

4. **DolRecomp (LLVM).** Set `LLVM_DIR` (and `VSDEVCMD` if your VS
   install differs), then `tools\windows\build-dolrecomp-llvm.bat`.
   Keep build trees and the DolRecomp cache on a roomy drive
   (`MGS_BUILD_ROOT`, `MGS_LOCALAPPDATA`): generation filled a system
   drive once during bring-up.

5. **Generate + link the module.** Generate DOL and REL objects with
   upstream's LLVM flow (`tools/rel-dol-entries.py`,
   `tools/dol-ranges.py`; `DOLRECOMP_ENTER_DATA_REFS=1`,
   `DOLRECOMP_EXTERNAL_RANGES`, `MEM2_BASE=0x3E000000` for the REL),
   then set `LLVM_MINGW_BIN`, `DOL_GENERATED`, `REL_GENERATED` and run
   `tools\windows\build-module-lld.bat`. Output:
   `module-lld\gGGSPA4_recomp.dll`. If a long repo path breaks the
   build (MAX_PATH / response files), map a short drive with
   `subst X: <repo>` and build from there.

6. **Play.** Set `SDL3_BIN`, then `tools\windows\play.bat`.
   Keys: Enter=Start, X=A, Z=B, S=X, A=Y, Q/W=L/R, E=Z, arrows=D-pad,
   Esc quits. Gamepad works too.

## Verified state (2026-11-02)

- Host: 22/22 `ctest` pass from the repo root (MinGW GCC 13.1,
  SDL3 3.4.18); both PAL discs mount, hashes match `config/GGSPA4.toml`.
- Module: DolRecomp LLVM backend, 2,287 DOL + 21,594 REL native
  objects, linked with clang+lld.
- Run: windowed Vulkan at 512x448, past the Konami logo into the intro
  FMV, title, codec (Campbell, 140.85) and the playable Dock area;
  extended scripted runs reached ~frame 13,920 with no faults.
  Scripted keyboard input drove every menu transition.

Not yet proven on Windows: a full human playthrough, saving/loading,
and later areas. US/JP builds are separate per-build ports (own
config, symbols and module); the Japanese Special Disc (`PGSJ01`) is
a different title and can never be mounted as a Twin Snakes disc.
