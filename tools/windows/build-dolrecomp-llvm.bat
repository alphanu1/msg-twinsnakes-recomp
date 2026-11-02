@echo off
rem Build DolRecomp with its LLVM backend (MSVC). The LLVM backend is the
rem upstream mainline: it emits native objects instead of C.
rem
rem Required environment:
rem   LLVM_DIR    LLVM CMake package dir (lib/cmake/llvm) from an LLVM 19/20
rem               dev tree. The official Windows installer does NOT ship
rem               LLVMConfig.cmake or the static libraries; extract the
rem               include/, lib/ and bin/ subsets from the
rem               clang+llvm-*-x86_64-pc-windows-msvc.tar.xz release instead.
rem Optional:
rem   VSDEVCMD        default: VS 18 Professional VsDevCmd.bat
rem   MGS_BUILD_ROOT  default: <repo>\build  (put this on a roomy drive;
rem                   DolRecomp's object cache is large - also consider
rem                   pointing LOCALAPPDATA at it, see the Windows doc)
rem Prerequisites: tools/bootstrap.sh has fetched extern/DolRecomp and
rem applied tools/patches/*.patch (keep those patch files LF - .gitattributes).
setlocal
set ROOT=%~dp0..\..
if not defined VSDEVCMD set VSDEVCMD=C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat
if not exist "%VSDEVCMD%" (echo VsDevCmd.bat not found - set VSDEVCMD. & exit /b 2)
if not defined LLVM_DIR (echo Set LLVM_DIR to the LLVM CMake package dir. & exit /b 2)
if not defined MGS_BUILD_ROOT set MGS_BUILD_ROOT=%ROOT%\build
call "%VSDEVCMD%" -arch=x64 >nul
cmake -S %ROOT%\extern\DolRecomp -B %MGS_BUILD_ROOT%\dolrecomp-llvm -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DDOLRECOMP_ENABLE_LLVM=ON -DLLVM_DIR=%LLVM_DIR%
if errorlevel 1 exit /b 1
cmake --build %MGS_BUILD_ROOT%\dolrecomp-llvm --target dolrecomp -j 4
exit /b %errorlevel%
