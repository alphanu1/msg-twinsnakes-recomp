@echo off
rem Native Windows host build (MinGW GCC). MSVC cannot build this tree:
rem the runtime uses GCC-style atomics and pthread idioms throughout.
rem
rem Required environment:
rem   MGS_QT_TOOLS        Qt Tools root holding mingw1310_64\bin, Ninja and
rem                       CMake_64\bin (e.g. C:\Qt\Tools)
rem   SDL3_DIR            SDL3 CMake package dir (.../lib/cmake/SDL3)
rem   MGS_PREBUILT_SHADERS  dir of prebuilt SPIR-V .inc files, when no GLSL
rem                       compiler (glslc/glslangValidator) is available
rem Optional:
rem   BUILD_DIR           default: <repo>\build-mingw
setlocal
set ROOT=%~dp0..\..
if not defined MGS_QT_TOOLS (echo Set MGS_QT_TOOLS to your Qt Tools root ^(mingw1310_64, Ninja, CMake_64^). & exit /b 2)
if not defined SDL3_DIR (echo Set SDL3_DIR to the SDL3 CMake package dir. & exit /b 2)
if not defined BUILD_DIR set BUILD_DIR=%ROOT%\build-mingw
set PATH=%MGS_QT_TOOLS%\mingw1310_64\bin;%MGS_QT_TOOLS%\Ninja;%MGS_QT_TOOLS%\CMake_64\bin;%PATH%
cd /d %ROOT%
set SHADER_ARG=
if defined MGS_PREBUILT_SHADERS set SHADER_ARG=-DMGS_PREBUILT_SHADERS=%MGS_PREBUILT_SHADERS%
cmake -S . -B %BUILD_DIR% -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DSDL3_DIR=%SDL3_DIR% %SHADER_ARG%
if errorlevel 1 exit /b 1
cmake --build %BUILD_DIR% -j 4
exit /b %errorlevel%
