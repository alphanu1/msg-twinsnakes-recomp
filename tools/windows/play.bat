@echo off
rem Launch the native Windows build (PAL GGSPA4, native LLVM module).
rem Keys: Enter=Start  X=A  Z=B  S=X  A=Y  Q/W=L/R  E=Z  arrows=D-pad
rem (gamepad also works; Esc quits)
rem
rem Required environment:
rem   MGS_QT_TOOLS   Qt Tools root (for the MinGW runtime DLLs)
rem   SDL3_BIN       dir holding SDL3.dll
rem Optional:
rem   BUILD_DIR      host build dir, default: <repo>\build-mingw
rem   MGS_BUILD_ROOT module build root, default: <repo>\build
rem   MODULE_DLL     default: %MGS_BUILD_ROOT%\module-lld\gGGSPA4_recomp.dll
setlocal
set ROOT=%~dp0..\..
if not defined MGS_QT_TOOLS (echo Set MGS_QT_TOOLS to your Qt Tools root. & exit /b 2)
if not defined SDL3_BIN (echo Set SDL3_BIN to the dir holding SDL3.dll. & exit /b 2)
if not defined BUILD_DIR set BUILD_DIR=%ROOT%\build-mingw
if not defined MGS_BUILD_ROOT set MGS_BUILD_ROOT=%ROOT%\build
if not defined MODULE_DLL set MODULE_DLL=%MGS_BUILD_ROOT%\module-lld\gGGSPA4_recomp.dll
set PATH=%MGS_QT_TOOLS%\mingw1310_64\bin;%SDL3_BIN%;%PATH%
cd /d %ROOT%
%BUILD_DIR%\host\twin-snakes.exe --play --module %MODULE_DLL%
pause
