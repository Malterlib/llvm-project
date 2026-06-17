@echo off

REM Filter out tests that are known to fail.
set "LIT_FILTER_OUT=gh110231.cpp|crt_initializers.cpp|init-order-atexit.cpp|use_after_return_linkage.cpp|initialization-bug.cpp|initialization-bug-no-global.cpp|trace-malloc-unbalanced.test|trace-malloc-2.test|TraceMallocTest"

setlocal enabledelayedexpansion
goto begin

:usage
echo Script for building the LLVM installer on Windows,
echo used for the releases at https://github.com/llvm/llvm-project/releases
echo.
echo Usage: build_llvm_release.bat --version ^<version^> [--x86,--x64, --arm64] [--skip-checkout] [--local-python] [--force-msvc] [--asan] [--stage0-only] [--pgo-only] [--skip-stage0] [--final-stage-only]
echo.
echo Options:
echo --version: [required] version to build
echo --help: display this help
echo --x86: build and test x86 variant
echo --x64: build and test x64 variant
echo --arm64: build and test arm64 variant
echo --skip-checkout: use local git checkout instead of downloading src.zip
echo --local-python: use installed Python and does not try to use a specific version (3.11)
echo --force-msvc: use MSVC compiler for stage0, even if clang-cl is present
echo --no-debug: build without debug info (much smaller objects/libs)
echo --install-prefix DIR: install the toolchain to DIR instead of building a CPack/NSIS package
echo --cleanup: delete throwaway build trees (PGO instrument/train, stage0 objects) during the build
echo --asan: build only the final stage with AddressSanitizer; disables final-stage rpmalloc because LLVM forbids combining them
echo --stage0-only: build the stage0 compiler/dependencies and stop
echo --pgo-only: reuse an existing stage0 compiler/dependencies, generate the PGO profile, and stop before the final stage
echo --skip-stage0: reuse an existing stage0 compiler/dependencies, then run PGO training and the final stage
echo --final-stage-only: build the final stage from an existing PGO profile
echo.
echo Note: At least one variant to build is required.
echo.
echo Example: build_llvm_release.bat --version 15.0.0 --x86 --x64
exit /b 1


:begin

::==============================================================================
:: parse args
set version=
set help=
set x86=
set x64=
set arm64=
set skip-checkout=
set local-python=
set force-msvc=
set no-debug=
set install-prefix=
set cleanup=
set asan=
set stage0-only=
set pgo-only=
set skip-stage0=
set final-stage-only=
call :parse_args %*

if "%final-stage-only%" == "true" set skip-stage0=true
if "%pgo-only%" == "true" set skip-stage0=true

if "%help%" NEQ "" goto usage

if "%version%" == "" (
    echo --version option is required
    echo =============================
    goto usage
)

if "%arm64%" == "" if "%x64%" == "" if "%x86%" == "" (
    echo nothing to build!
    echo choose one or several variants from: --x86 --x64 --arm64
    exit /b 1
)

if "%pgo-only%" == "true" if "%x86%" == "true" (
    echo --pgo-only is only supported for x64 and arm64 builds.
    exit /b 1
)

::==============================================================================
:: check prerequisites
REM Note:
REM   7zip versions 21.x and higher will try to extract the symlinks in
REM   llvm's git archive, which requires running as administrator.

REM Check 7-zip version and/or administrator permissions.
if not "%skip-stage0%" == "true" (
  for /f "delims=" %%i in ('7z.exe ^| findstr /r "2[1-9].[0-9][0-9]"') do set version_7z=%%i
  if not "%version_7z%"=="" (
    REM Unique temporary filename to use by the 'mklink' command.
    set "link_name=%temp%\%username%_%random%_%random%.tmp"

    REM As the 'mklink' requires elevated permissions, the symbolic link
    REM creation will fail if the script is not running as administrator.
    mklink /d "!link_name!" . 1>nul 2>nul
    if errorlevel 1 (
      echo.
      echo Script requires administrator permissions, or a 7-zip version 20.x or older.
      echo Current version is "%version_7z%"
      exit /b 1
    ) else (
      REM Remove the temporary symbolic link.
      rd "!link_name!"
    )
  )
)

REM Prerequisites:
REM
REM   Visual Studio 2019, CMake, Ninja, GNUWin32, SWIG, Python 3,
REM   NSIS with the strlen_8192 patch,
REM   Perl (for the OpenMP run-time).
REM
REM
REM   For LLDB, SWIG version 4.1.1 should be used.
REM

:: Detect Visual Studio
set vsinstall=
set vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe

if "%VSINSTALLDIR%" NEQ "" (
  echo using enabled Visual Studio installation
  set "vsinstall=%VSINSTALLDIR%"
) else (
  echo using vswhere to detect Visual Studio installation
  FOR /F "delims=" %%r IN ('^""%vswhere%" -nologo -latest -products "*" -all -property installationPath^"') DO set vsinstall=%%r
)
set "vsdevcmd=%vsinstall%\Common7\Tools\VsDevCmd.bat"

if not exist "%vsdevcmd%" (
  echo Can't find any installation of Visual Studio
  exit /b 1
)
echo Using VS devcmd: %vsdevcmd%

::==============================================================================
:: start echoing what we do
@echo on

REM These can be overridden from the environment (e.g. by the Malterlib build
REM script) so the Python used for the build stays in sync with prerequisite
REM checks. They default to the per-user python.org install locations.
if not defined python32_dir set python32_dir=C:\Users\%USERNAME%\AppData\Local\Programs\Python\Python311-32
if not defined python64_dir set python64_dir=C:\Users\%USERNAME%\AppData\Local\Programs\Python\Python311
if not defined pythonarm64_dir set pythonarm64_dir=C:\Users\%USERNAME%\AppData\Local\Programs\Python\Python311-arm64
if not defined LLDB_PDCURSES_SOURCE_DIR set "LLDB_PDCURSES_SOURCE_DIR=%~dp0..\..\..\..\PDCursesMod"
if not defined LLDB_PDCURSES_TARGET set "LLDB_PDCURSES_TARGET=wincon_pdcursesstatic"
for %%i in ("%LLDB_PDCURSES_SOURCE_DIR%") do set "LLDB_PDCURSES_SOURCE_DIR=%%~fi"
if not exist "%LLDB_PDCURSES_SOURCE_DIR%\CMakeLists.txt" (
  echo Can't find LLDB PDCurses source: %LLDB_PDCURSES_SOURCE_DIR%
  exit /b 1
)
echo Using LLDB PDCurses source: %LLDB_PDCURSES_SOURCE_DIR%
echo Using LLDB PDCurses target: %LLDB_PDCURSES_TARGET%

REM Build LLDB with Python scripting. Require it explicitly so a missing SWIG or
REM Python (dev headers/libs) fails the build loudly instead of silently shipping
REM an LLDB without scripting support. A relative LLDB_PYTHON_HOME is resolved at
REM runtime relative to the liblldb directory (bin/), so a Python runtime bundled
REM in a python subdirectory beside liblldb makes the distribution portable - no
REM absolute build path is baked in. The bundling is done in
REM BuildLLVMDistributionWindows.sh.
REM
REM LLDB's curses GUI is built against PDCurses. The FindCursesAndPanel module
REM adds the target from this source directory so it inherits LLVM's final-stage
REM compiler, runtime, and linker settings.
set "pdcurses_source_dir=%LLDB_PDCURSES_SOURCE_DIR:\=/%"
set common_lldb_flags=^
  -DLLDB_ENABLE_PYTHON=ON ^
  -DLLDB_EMBED_PYTHON_HOME=ON ^
  -DLLDB_PYTHON_HOME=python
set lldb_curses_flags=^
  -DLLDB_ENABLE_CURSES=ON ^
  -DLLDB_PDCURSES_SOURCE_DIR="%pdcurses_source_dir%" ^
  -DLLDB_PDCURSES_TARGET="%LLDB_PDCURSES_TARGET%" ^
  -DPDC_BUILD_SHARED=OFF ^
  -DPDC_UTF8=ON ^
  -DPDC_WIDE=ON ^
  -DPDC_CHTYPE_32=OFF ^
  -DPDC_NCURSES_BUILD=OFF ^
  -DPDC_SDL2_BUILD=OFF ^
  -DPDC_SDL2_DEPS_BUILD=OFF ^
  -DPDC_GL_BUILD=OFF ^
  -DPDC_VT_BUILD=OFF ^
  -DPDC_WINCON_BUILD=ON ^
  -DPDC_WINGUI_BUILD=OFF

set revision=llvmorg-%version%
set package_version=%version%
set build_dir=%cd%\llvm_package_%package_version%
set "tar_tool=tar"
if not "%LLVM_BSDTAR%" == "" set "tar_tool=%LLVM_BSDTAR%"

echo Revision: %revision%
echo Package version: %package_version%
echo Build dir: %build_dir%
echo Tar tool: %tar_tool%
echo.

::if exist %build_dir% (
::  echo Build directory already exists: %build_dir%
::  exit /b 1
::)
if not exist "%build_dir%" mkdir "%build_dir%"
cd %build_dir% || exit /b 1

if "%skip-checkout%" == "true" (
  echo Using local source
  set llvm_src=%~dp0..\..\..
) else if "%final-stage-only%" == "true" (
  echo Using existing source
  set llvm_src=%build_dir%\llvm-project
  if not exist "%build_dir%\llvm-project\llvm\CMakeLists.txt" (
    echo Existing source checkout is missing: %build_dir%\llvm-project
    echo Use --skip-checkout with --final-stage-only to rebuild from the local source tree, or run without --final-stage-only once.
    exit /b 1
  )
) else (
  echo Checking out %revision%
  curl -L https://github.com/llvm/llvm-project/archive/%revision%.zip -o src.zip || exit /b 1
  7z x src.zip || exit /b 1
  mv llvm-project-* llvm-project || exit /b 1
  set llvm_src=%build_dir%\llvm-project
)

if not "%skip-stage0%" == "true" (
  curl -O https://gitlab.gnome.org/GNOME/libxml2/-/archive/v2.9.12/libxml2-v2.9.12.tar.gz || exit /b 1
  "%tar_tool%" -xzf libxml2-v2.9.12.tar.gz || exit /b 1
)

REM Setting CMAKE_CL_SHOWINCLUDES_PREFIX to work around PR27226.
REM Common flags for all builds.
set debug_flags=-g -fno-limit-debug-info /Zi
set debug_linker_flags=/INCREMENTAL:NO /DEBUG /OPT:REF /OPT:ICF
if "%no-debug%" == "true" (
  set debug_flags=
  set debug_linker_flags=/INCREMENTAL:NO
)
set parallel_link_cmake_flags=
if not "%LLVM_PARALLEL_LINK_JOBS%" == "" (
  set parallel_link_cmake_flags=-DLLVM_PARALLEL_LINK_JOBS=%LLVM_PARALLEL_LINK_JOBS%
  echo LLVM parallel link jobs: %LLVM_PARALLEL_LINK_JOBS%
) else (
  echo LLVM parallel link jobs: unset
)
set common_compiler_flags=-DLIBXML_STATIC %debug_flags%
set common_cmake_flags=^
  -DLLVM_USE_SYMLINKS=ON ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DLLVM_ENABLE_ASSERTIONS=OFF ^
  -DLLVM_INSTALL_TOOLCHAIN_ONLY=ON ^
  %parallel_link_cmake_flags% ^
  -DLLVM_TARGETS_TO_BUILD="AArch64;ARM;X86;BPF;WebAssembly;RISCV;NVPTX" ^
  -DLLVM_BUILD_LLVM_C_DYLIB=ON ^
  -DPython3_FIND_REGISTRY=NEVER ^
  -DPACKAGE_VERSION="%package_version%" ^
  -DCMAKE_CL_SHOWINCLUDES_PREFIX="Note: including file: " ^
  -DLLVM_ENABLE_LIBXML2=FORCE_ON ^
  -DCLANG_ENABLE_LIBXML2=OFF ^
  -DCMAKE_C_FLAGS="%common_compiler_flags%" ^
  -DCMAKE_CXX_FLAGS="%common_compiler_flags%" ^
  -DCMAKE_EXE_LINKER_FLAGS_RELEASE="%debug_linker_flags%" ^
  -DCMAKE_MODULE_LINKER_FLAGS_RELEASE="%debug_linker_flags%" ^
  -DCMAKE_SHARED_LINKER_FLAGS_RELEASE="%debug_linker_flags%" ^
  -DLLVM_ENABLE_RPMALLOC=ON ^
  -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;lld" ^
  -DLLVM_ENABLE_RUNTIMES="compiler-rt;openmp" ^
  -DCOMPILER_RT_BUILD_ORC=OFF
if not "%install-prefix%" == "" set common_cmake_flags=%common_cmake_flags% -DCMAKE_INSTALL_PREFIX="%install-prefix%"

if "%force-msvc%" == "" (
  where /q clang-cl
  if %errorlevel% EQU 0 (
    where /q lld-link
    if %errorlevel% EQU 0 (
      set common_compiler_flags=%common_compiler_flags% -fuse-ld=lld
      
      set common_cmake_flags=%common_cmake_flags%^
        -DCMAKE_C_COMPILER=clang-cl.exe ^
        -DCMAKE_CXX_COMPILER=clang-cl.exe ^
        -DCMAKE_LINKER=lld-link.exe ^
        -DLLVM_ENABLE_LLD=ON ^
        -DCMAKE_C_FLAGS="%common_compiler_flags%" ^
        -DCMAKE_CXX_FLAGS="%common_compiler_flags%"
    )
  )
)

set cmake_profile_flags=""

REM Preserve original path
set OLDPATH=%PATH%

REM Build the 32-bits and/or 64-bits binaries.
if "%x86%" == "true" call :do_build_32 || exit /b 1
if "%x64%" == "true" call :do_build_64_common amd64 %python64_dir% || exit /b 1
if "%arm64%" == "true" call :do_build_64_common arm64 %pythonarm64_dir% || exit /b 1
exit /b 0

::==============================================================================
:: Build 32-bits binaries.
::==============================================================================
:do_build_32
call :set_environment %python32_dir% || exit /b 1
call "%vsdevcmd%" -arch=x86 || exit /b 1
@echo on
if not exist build32_stage0 mkdir build32_stage0
cd build32_stage0
if "%skip-stage0%" == "true" (
  set "libxmldir=%build_dir%\build32_stage0\libxmlbuild\install"
  if not exist "%build_dir%\build32_stage0\libxmlbuild\install\lib\libxml2s.lib" (
    echo Stage0 libxml install is missing: %build_dir%\build32_stage0\libxmlbuild\install
    echo Run the stage0 build first to create the staged dependencies.
    exit /b 1
  )
  set "libxmldir=!libxmldir:\=/!"
) else (
  call :do_build_libxml || exit /b 1
)

REM Stage0 binaries directory; used in stage1.
set "stage0_bin_dir=%build_dir%/build32_stage0/bin"
set cmake_flags=^
  %common_cmake_flags% ^
  -DLLVM_ENABLE_RPMALLOC=OFF ^
  -DLLDB_TEST_COMPILER="%stage0_bin_dir%/clang.exe" ^
  -DPYTHON_HOME="%PYTHONHOME%" ^
  -DPython3_ROOT_DIR="%PYTHONHOME%" ^
  -DLIBXML2_INCLUDE_DIR=%libxmldir%/include/libxml2 ^
  -DLIBXML2_LIBRARIES=%libxmldir%/lib/libxml2s.lib

if "%skip-stage0%" == "true" if not exist "%stage0_bin_dir%/clang-cl.exe" (
  echo Stage0 compiler is missing: %stage0_bin_dir%/clang-cl.exe
  echo Run the stage0 build first to create the staged compiler.
  exit /b 1
)
if not "%skip-stage0%" == "true" (
  cmake -GNinja %cmake_flags% %llvm_src%\llvm || exit /b 1
  ninja || ninja || ninja || exit /b 1
  REM ninja check-llvm || ninja check-llvm || ninja check-llvm || exit /b 1
  REM ninja check-clang || ninja check-clang || ninja check-clang || exit /b 1
  REM ninja check-lld || ninja check-lld || ninja check-lld || exit /b 1
  REM ninja check-runtimes || ninja check-runtimes || ninja check-runtimes || exit /b 1
  REM ninja check-clang-tools || ninja check-clang-tools || ninja check-clang-tools || exit /b 1
)
cd..
if "%stage0-only%" == "true" exit /b 0

REM CMake expects the paths that specifies the compiler and linker to be
REM with forward slash.
set all_cmake_flags=^
  %cmake_flags% ^
  -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;lld;lldb;" ^
  %common_lldb_flags% ^
  %lldb_curses_flags% ^
  -DPYTHON_HOME="%PYTHONHOME%" ^
  -DCMAKE_C_COMPILER=%stage0_bin_dir%/clang-cl.exe ^
  -DCMAKE_CXX_COMPILER=%stage0_bin_dir%/clang-cl.exe ^
  -DCMAKE_LINKER=%stage0_bin_dir%/lld-link.exe ^
  -DCMAKE_AR=%stage0_bin_dir%/llvm-lib.exe ^
  -DCMAKE_RC=%stage0_bin_dir%/llvm-windres.exe
set cmake_flags=%all_cmake_flags:\=/%

if not exist build32 mkdir build32
cd build32
call :setup_final_stage_sanitizer "%build_dir%\build32_stage0" || exit /b 1
cmake -GNinja %cmake_flags% ^
  %final_stage_sanitizer_flags% ^
  %llvm_src%\llvm || exit /b 1
ninja || ninja || ninja || exit /b 1
REM ninja check-llvm || ninja check-llvm || ninja check-llvm || exit /b 1
REM ninja check-clang || ninja check-clang || ninja check-clang || exit /b 1
REM ninja check-lld || ninja check-lld || ninja check-lld || exit /b 1
REM ninja check-runtimes || ninja check-runtimes || ninja check-runtimes || exit /b 1
REM ninja check-clang-tools || ninja check-clang-tools || ninja check-clang-tools || exit /b 1
if "%install-prefix%" == "" (ninja package || exit /b 1) else (ninja install || exit /b 1)
cd ..

exit /b 0
::==============================================================================

::==============================================================================
:: Build 64-bits binaries (common function for both x64 and arm64)
::==============================================================================
:do_build_64_common
set arch=%1
set python_dir=%2

call :set_environment %python_dir% || exit /b 1
call "%vsdevcmd%" -arch=%arch% || exit /b 1
@echo on
if not exist build_%arch%_stage0 mkdir build_%arch%_stage0
cd build_%arch%_stage0
if "%skip-stage0%" == "true" (
  set "libxmldir=%build_dir%\build_%arch%_stage0\libxmlbuild\install"
  if not exist "%build_dir%\build_%arch%_stage0\libxmlbuild\install\lib\libxml2s.lib" (
    echo Stage0 libxml install is missing: %build_dir%\build_%arch%_stage0\libxmlbuild\install
    echo Run the stage0 build first to create the staged dependencies.
    exit /b 1
  )
  set "libxmldir=!libxmldir:\=/!"
) else (
  call :do_build_libxml || exit /b 1
)

REM Stage0 binaries directory; used in stage1.
set "stage0_bin_dir=%build_dir%/build_%arch%_stage0/bin"
set cmake_flags=^
  %common_cmake_flags% ^
  -DLLDB_TEST_COMPILER="%stage0_bin_dir%/clang.exe" ^
  -DPYTHON_HOME="%PYTHONHOME%" ^
  -DPython3_ROOT_DIR="%PYTHONHOME%" ^
  -DLIBXML2_INCLUDE_DIR=%libxmldir%/include/libxml2 ^
  -DLIBXML2_LIBRARIES=%libxmldir%/lib/libxml2s.lib ^
  -DCLANG_DEFAULT_LINKER=lld
if "%arch%"=="arm64" (
  set cmake_flags=%cmake_flags% ^
    -DCOMPILER_RT_BUILD_SANITIZERS=OFF
)

if "%skip-stage0%" == "true" if not exist "%stage0_bin_dir%/clang-cl.exe" (
  echo Stage0 compiler is missing: %stage0_bin_dir%/clang-cl.exe
  echo Run the stage0 build first to create the staged compiler.
  exit /b 1
)
if not "%skip-stage0%" == "true" (
  cmake -GNinja %cmake_flags% ^
    -DLLVM_TARGETS_TO_BUILD=Native ^
    %llvm_src%\llvm || exit /b 1
  ninja || ninja || ninja || exit /b 1
  if "%cleanup%" == "true" del /s /q *.obj 1>nul 2>nul
  REM ninja check-llvm || ninja check-llvm || ninja check-llvm || exit /b 1
  REM ninja check-clang || ninja check-clang || ninja check-clang || exit /b 1
  REM ninja check-lld || ninja check-lld || ninja check-lld || exit /b 1
  REM ninja check-runtimes || ninja check-runtimes || ninja check-runtimes || exit /b 1
  REM ninja check-clang-tools || ninja check-clang-tools || ninja check-clang-tools || exit /b 1
  REM ninja check-clangd || ninja check-clangd || ninja check-clangd || exit /b 1
)
cd..
if "%stage0-only%" == "true" exit /b 0

REM CMake expects the paths that specifies the compiler and linker to be
REM with forward slash.
set all_cmake_flags=^
  %cmake_flags% ^
  -DCMAKE_C_COMPILER=%stage0_bin_dir%/clang-cl.exe ^
  -DCMAKE_CXX_COMPILER=%stage0_bin_dir%/clang-cl.exe ^
  -DCMAKE_LINKER=%stage0_bin_dir%/lld-link.exe ^
  -DCMAKE_AR=%stage0_bin_dir%/llvm-lib.exe ^
  -DCMAKE_RC=%stage0_bin_dir%/llvm-windres.exe
if "%arch%"=="arm64" (
  set all_cmake_flags=%all_cmake_flags% ^
    -DCPACK_SYSTEM_NAME=woa64
)
set cmake_flags=%all_cmake_flags:\=/%

if not exist build_%arch% mkdir build_%arch%
cd build_%arch%
if "%final-stage-only%" == "true" (
  if not exist "%build_dir%\build_%arch%\profile.profdata" (
    echo Final stage profile is missing: %build_dir%\build_%arch%\profile.profdata
    echo Run the PGO build first to generate the profile.
    exit /b 1
  )
  set "profile=%build_dir%\build_%arch%\profile.profdata"
  set "profile=!profile:\=/!"
  set "profile_compiler_flags=!common_compiler_flags! -Wno-backend-plugin"
  set cmake_profile_flags=-DLLVM_PROFDATA_FILE=!profile! ^
    -DCMAKE_C_FLAGS="!profile_compiler_flags!" ^
    -DCMAKE_CXX_FLAGS="!profile_compiler_flags!"
) else (
  call :do_generate_profile || exit /b 1
)
if "%pgo-only%" == "true" exit /b 0
call :setup_final_stage_sanitizer "%build_dir%\build_%arch%_stage0" || exit /b 1
cmake -GNinja %cmake_flags% ^
  %final_stage_sanitizer_flags% ^
  -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;lld;lldb;flang;mlir" ^
  %common_lldb_flags% ^
  %lldb_curses_flags% ^
  -DPYTHON_HOME="%PYTHONHOME%" ^
  %cmake_profile_flags% %llvm_src%\llvm || exit /b 1
ninja || ninja || ninja || exit /b 1
REM ninja check-llvm || ninja check-llvm || ninja check-llvm || exit /b 1
REM ninja check-clang || ninja check-clang || ninja check-clang || exit /b 1
REM ninja check-lld || ninja check-lld || ninja check-lld || exit /b 1
REM ninja check-runtimes || ninja check-runtimes || ninja check-runtimes || exit /b 1
REM ninja check-clang-tools || ninja check-clang-tools || ninja check-clang-tools || exit /b 1
REM ninja check-clangd || ninja check-clangd || ninja check-clangd || exit /b 1
REM ninja check-flang || ninja check-flang || ninja check-flang || exit /b 1
REM ninja check-mlir || ninja check-mlir || ninja check-mlir || exit /b 1
REM ninja check-lldb || ninja check-lldb || ninja check-lldb || exit /b 1
if "%install-prefix%" == "" (ninja package || exit /b 1) else (ninja install || exit /b 1)

:: generate tarball with install toolchain only off
REM if "%arch%"=="amd64" (
REM   set filename=clang+llvm-%version%-x86_64-pc-windows-msvc
REM ) else (
REM   set filename=clang+llvm-%version%-aarch64-pc-windows-msvc
REM )
REM cmake -GNinja %cmake_flags% %cmake_profile_flags% -DLLVM_INSTALL_TOOLCHAIN_ONLY=OFF ^
REM   -DCMAKE_INSTALL_PREFIX=%build_dir%/%filename% %llvm_src%\llvm || exit /b 1
REM ninja install || exit /b 1
:: check llvm_config is present & returns something
::%build_dir%/%filename%/bin/llvm-config.exe --bindir || exit /b 1
cd ..
::7z a -ttar -so %filename%.tar %filename% | 7z a -txz -si %filename%.tar.xz

exit /b 0

::==============================================================================
:: Set PATH and some environment variables.
::==============================================================================
:set_environment
REM Restore original path
set PATH=%OLDPATH%

set python_dir=%1

REM Set Python environment
if "%local-python%" == "true" (
  FOR /F "delims=" %%i IN ('where python.exe ^| head -1') DO set python_exe=%%i
  set PYTHONHOME=!python_exe:~0,-11!
) else (
  %python_dir%/python.exe --version || exit /b 1
  set PYTHONHOME=%python_dir%
)
set PATH=%PYTHONHOME%;%PATH%

set "VSCMD_START_DIR=%build_dir%"

exit /b 0

::=============================================================================

::==============================================================================
:: Configure final-stage sanitizer flags.
::==============================================================================
:setup_final_stage_sanitizer
set final_stage_sanitizer_flags=-DLLVM_USE_SANITIZER=

if "%asan%" == "true" (
  set final_stage_sanitizer_flags=^
    -DLLVM_USE_SANITIZER=Address ^
    -DLLVM_ENABLE_RPMALLOC=OFF

  set "asan_runtime_dir="
  for /d %%i in ("%~1\lib\clang\*") do set "asan_runtime_dir=%%~fi\lib\windows"

  if not defined asan_runtime_dir (
    echo Unable to find stage0 Clang resource directory under: %~1\lib\clang
    exit /b 1
  )

  dir /b "!asan_runtime_dir!\clang_rt.asan_dynamic-*.dll" 1>nul 2>nul
  if errorlevel 1 (
    echo Unable to find AddressSanitizer runtime DLLs in: !asan_runtime_dir!
    exit /b 1
  )

  echo Building final stage with AddressSanitizer runtime from: !asan_runtime_dir!
  set "PATH=!asan_runtime_dir!;%PATH%"
)

exit /b 0

::=============================================================================

::==============================================================================
:: Build libxml.
::==============================================================================
:do_build_libxml
mkdir libxmlbuild
cd libxmlbuild
cmake -GNinja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=install ^
  -DBUILD_SHARED_LIBS=OFF -DLIBXML2_WITH_C14N=OFF -DLIBXML2_WITH_CATALOG=OFF ^
  -DLIBXML2_WITH_DEBUG=OFF -DLIBXML2_WITH_DOCB=OFF -DLIBXML2_WITH_FTP=OFF ^
  -DLIBXML2_WITH_HTML=OFF -DLIBXML2_WITH_HTTP=OFF -DLIBXML2_WITH_ICONV=OFF ^
  -DLIBXML2_WITH_ICU=OFF -DLIBXML2_WITH_ISO8859X=OFF -DLIBXML2_WITH_LEGACY=OFF ^
  -DLIBXML2_WITH_LZMA=OFF -DLIBXML2_WITH_MEM_DEBUG=OFF -DLIBXML2_WITH_MODULES=OFF ^
  -DLIBXML2_WITH_OUTPUT=ON -DLIBXML2_WITH_PATTERN=OFF -DLIBXML2_WITH_PROGRAMS=OFF ^
  -DLIBXML2_WITH_PUSH=OFF -DLIBXML2_WITH_PYTHON=OFF -DLIBXML2_WITH_READER=OFF ^
  -DLIBXML2_WITH_REGEXPS=OFF -DLIBXML2_WITH_RUN_DEBUG=OFF -DLIBXML2_WITH_SAX1=ON ^
  -DLIBXML2_WITH_SCHEMAS=OFF -DLIBXML2_WITH_SCHEMATRON=OFF -DLIBXML2_WITH_TESTS=OFF ^
  -DLIBXML2_WITH_THREADS=ON -DLIBXML2_WITH_THREAD_ALLOC=OFF -DLIBXML2_WITH_TREE=ON ^
  -DLIBXML2_WITH_VALID=OFF -DLIBXML2_WITH_WRITER=OFF -DLIBXML2_WITH_XINCLUDE=OFF ^
  -DLIBXML2_WITH_XPATH=OFF -DLIBXML2_WITH_XPTR=OFF -DLIBXML2_WITH_ZLIB=OFF ^
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded ^
  ../../libxml2-v2.9.12 || exit /b 1
ninja install || exit /b 1
set libxmldir=%cd%\install
set "libxmldir=%libxmldir:\=/%"
cd ..
exit /b 0

::==============================================================================
:: Generate a PGO profile.
::==============================================================================
:do_generate_profile
REM Build Clang with instrumentation.
mkdir instrument
cd instrument
cmake -GNinja %cmake_flags% -DLLVM_TARGETS_TO_BUILD=Native ^
  -DLLVM_BUILD_INSTRUMENTED=IR %llvm_src%\llvm || exit /b 1
ninja clang || ninja clang || ninja clang || exit /b 1
set instrumented_clang=%cd:\=/%/bin/clang-cl.exe
cd ..
REM Use that to build part of llvm to generate a profile.
mkdir train
cd train
cmake -GNinja %cmake_flags% ^
  -DCMAKE_C_COMPILER=%instrumented_clang% ^
  -DCMAKE_CXX_COMPILER=%instrumented_clang% ^
  -DLLVM_ENABLE_PROJECTS=clang ^
  -DLLVM_TARGETS_TO_BUILD=Native ^
  %llvm_src%\llvm || exit /b 1
REM Drop profiles generated from running cmake; those are not representative.
del ..\instrument\profiles\*.profraw
ninja tools/clang/lib/Sema/CMakeFiles/obj.clangSema.dir/Sema.cpp.obj
cd ..
set profile=%cd:\=/%/profile.profdata
%stage0_bin_dir%\llvm-profdata merge -output=%profile% instrument\profiles\*.profraw || exit /b 1
if "%cleanup%" == "true" rd /s /q instrument train
set "common_compiler_flags=%common_compiler_flags% -Wno-backend-plugin"
set cmake_profile_flags=-DLLVM_PROFDATA_FILE=%profile% ^
  -DCMAKE_C_FLAGS="%common_compiler_flags%" ^
  -DCMAKE_CXX_FLAGS="%common_compiler_flags%"
exit /b 0

::=============================================================================
:: Parse command line arguments.
:: The format for the arguments is:
::   Boolean: --option
::   Value:   --option<separator>value
::     with <separator> being: space, colon, semicolon or equal sign
::
:: Command line usage example:
::   my-batch-file.bat --build --type=release --version 123
:: It will create 3 variables:
::   'build' with the value 'true'
::   'type' with the value 'release'
::   'version' with the value '123'
::
:: Usage:
::   set "build="
::   set "type="
::   set "version="
::
::   REM Parse arguments.
::   call :parse_args %*
::
::   if defined build (
::     ...
::   )
::   if %type%=='release' (
::     ...
::   )
::   if %version%=='123' (
::     ...
::   )
::=============================================================================
:parse_args
  set "arg_name="
  :parse_args_start
  if "%1" == "" (
    :: Set a seen boolean argument.
    if "%arg_name%" neq "" (
      set "%arg_name%=true"
    )
    goto :parse_args_done
  )
  set aux=%1
  if "%aux:~0,2%" == "--" (
    :: Set a seen boolean argument.
    if "%arg_name%" neq "" (
      set "%arg_name%=true"
    )
    set "arg_name=%aux:~2,250%"
  ) else (
    set "%arg_name%=%1"
    set "arg_name="
  )
  shift
  goto :parse_args_start

:parse_args_done
exit /b 0
