@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM Auto-detect VS toolchain and build Camera Proxy for x86/x64.
REM Usage:
REM   do_build.bat           -> builds x86 and x64
REM   do_build.bat x86       -> builds x86 only
REM   do_build.bat x64       -> builds x64 only
REM   do_build.bat both      -> builds x86 and x64

set "TARGET=%~1"
if "%TARGET%"=="" set "TARGET=both"

if /I not "%TARGET%"=="x86" if /I not "%TARGET%"=="x64" if /I not "%TARGET%"=="both" (
    echo ERROR: invalid target "%TARGET%".
    echo Usage: do_build.bat [x86^|x64^|both]
    exit /b 1
)

set "REPO_DIR=%~dp0"
pushd "%REPO_DIR%" >nul

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: vswhere not found at "%VSWHERE%".
    popd >nul
    exit /b 1
)

for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_INSTALL=%%I"
if "%VS_INSTALL%"=="" (
    echo ERROR: Could not find a Visual Studio installation with C++ tools.
    popd >nul
    exit /b 1
)

set "VSVARS=%VS_INSTALL%\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VSVARS%" (
    echo ERROR: vcvarsall.bat not found: "%VSVARS%".
    popd >nul
    exit /b 1
)

if exist build_log.txt del /q build_log.txt

echo Using Visual Studio at: %VS_INSTALL%
echo Using Visual Studio at: %VS_INSTALL%>> build_log.txt

if /I "%TARGET%"=="x86" (
    call :BuildOne x86
    set "RESULT=%ERRORLEVEL%"
    popd >nul
    exit /b %RESULT%
)
if /I "%TARGET%"=="x64" (
    call :BuildOne x64
    set "RESULT=%ERRORLEVEL%"
    popd >nul
    exit /b %RESULT%
)

call :BuildOne x86
if errorlevel 1 (
    popd >nul
    exit /b 1
)
call :BuildOne x64
if errorlevel 1 (
    popd >nul
    exit /b 1
)

echo.>> build_log.txt
echo Build completed for x86 and x64.>> build_log.txt

echo Build completed for x86 and x64.
popd >nul
exit /b 0

:BuildOne
set "ARCH=%~1"
set "OUT_DIR=build\%ARCH%"
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

echo.>> build_log.txt
echo ==== Building %ARCH% ====>> build_log.txt

call "%VSVARS%" %ARCH% >> build_log.txt 2>&1
if errorlevel 1 (
    echo Failed to initialize VS environment for %ARCH%.>> build_log.txt
    echo Failed to initialize VS environment for %ARCH%.
    exit /b 1
)

call build.bat %ARCH% >> build_log.txt 2>&1
if errorlevel 1 (
    echo Build failed for %ARCH%.>> build_log.txt
    echo Build failed for %ARCH%. See build_log.txt for details.
    exit /b 1
)

echo Build succeeded for %ARCH%.>> build_log.txt
echo Build succeeded for %ARCH%.
exit /b 0
