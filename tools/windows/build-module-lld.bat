@echo off
rem Link the combined DOL+REL native module with clang + lld.
rem Do NOT substitute GNU ld: it produces a corrupt ~560 MB DLL that
rem Windows refuses to load (error 193). lld's DLL is valid.
rem
rem Required environment:
rem   LLVM_MINGW_BIN    bin dir of an llvm-mingw toolchain (clang/lld)
rem   MGS_QT_TOOLS      Qt Tools root (Ninja + CMake_64 only)
rem   DOL_GENERATED     DolRecomp LLVM output dir for main.dol
rem   REL_GENERATED     DolRecomp LLVM output dir for the REL overlay
rem Optional:
rem   MGS_BUILD_ROOT    default: <repo>\build
rem   GXRUNTIME_DIR     default: extern\ModernGekko-Template\...\GXRuntime
rem   DOL_FILE          default: discs\GGSPA4\disc1\sys\main.dol
rem   REL_FILE          default: discs\GGSPA4\disc1\files\shared\mgso_pal.rel
rem   MGS_LOCALAPPDATA  if set, LOCALAPPDATA is redirected there (keeps the
rem                     large DolRecomp cache off a small system drive)
rem Generation of DOL_GENERATED/REL_GENERATED is upstream's LLVM flow
rem (tools/rel-dol-entries.py, tools/dol-ranges.py, DOLRECOMP_ENTER_DATA_REFS /
rem DOLRECOMP_EXTERNAL_RANGES / MEM2_BASE) - see docs/windows-native-build.md.
setlocal
set ROOT=%~dp0..\..
if not defined LLVM_MINGW_BIN (echo Set LLVM_MINGW_BIN to the llvm-mingw bin dir. & exit /b 2)
if not defined MGS_QT_TOOLS (echo Set MGS_QT_TOOLS to your Qt Tools root. & exit /b 2)
if not defined DOL_GENERATED (echo Set DOL_GENERATED to the DOL LLVM output dir. & exit /b 2)
if not defined REL_GENERATED (echo Set REL_GENERATED to the REL LLVM output dir. & exit /b 2)
if not defined MGS_BUILD_ROOT set MGS_BUILD_ROOT=%ROOT%\build
if not defined GXRUNTIME_DIR set GXRUNTIME_DIR=%ROOT%\extern\ModernGekko-Template\lib\ModernGekko\vendor\dolphin\GXRuntime
if not defined DOL_FILE set DOL_FILE=%ROOT%\discs\GGSPA4\disc1\sys\main.dol
if not defined REL_FILE set REL_FILE=%ROOT%\discs\GGSPA4\disc1\files\shared\mgso_pal.rel
if defined MGS_LOCALAPPDATA set LOCALAPPDATA=%MGS_LOCALAPPDATA%
set PATH=%LLVM_MINGW_BIN%;%MGS_QT_TOOLS%\Ninja;%MGS_QT_TOOLS%\CMake_64\bin;%PATH%
cmake -S %ROOT%\game\module -B %MGS_BUILD_ROOT%\module-lld -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DDOL_GENERATED=%DOL_GENERATED% -DREL_GENERATED=%REL_GENERATED% -DGXRUNTIME_DIR=%GXRUNTIME_DIR% -DDOL_FILE=%DOL_FILE% -DREL_FILE=%REL_FILE% -DREL_BASE=0x7F008000
if errorlevel 1 exit /b 1
cmake --build %MGS_BUILD_ROOT%\module-lld -j 4
exit /b %errorlevel%
