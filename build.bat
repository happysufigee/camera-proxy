@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM Build script for Camera Proxy (x86/x64)
REM Usage:
REM   build.bat           -> builds x86 and x64
REM   build.bat x86       -> builds x86 only
REM   build.bat x64       -> builds x64 only
REM   build.bat both      -> builds x86 and x64

set "TARGET=%~1"
if "%TARGET%"=="" set "TARGET=both"

if /I not "%TARGET%"=="x86" if /I not "%TARGET%"=="x64" if /I not "%TARGET%"=="both" (
    echo ERROR: invalid target "%TARGET%".
    echo Usage: build.bat [x86^|x64^|both]
    exit /b 1
)

where cl >nul 2>&1
if errorlevel 1 (
    echo ERROR: cl.exe not found. Run this from a Visual Studio Developer Command Prompt.
    exit /b 1
)

set "SOURCES=d3d9_proxy.cpp remix_lighting_manager.cpp lights_tab_ui.cpp custom_lights.cpp custom_lights_ui.cpp imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_tables.cpp imgui/imgui_widgets.cpp imgui/backends/imgui_impl_dx9.cpp imgui/backends/imgui_impl_win32.cpp"
set "COMMON_FLAGS=/nologo /LD /EHsc /O2 /MD"
set "LINK_FLAGS=/link /DEF:d3d9.def"

if /I "%TARGET%"=="x86" (
    call :BuildOne x86
    exit /b %ERRORLEVEL%
)
if /I "%TARGET%"=="x64" (
    call :BuildOne x64
    exit /b %ERRORLEVEL%
)

call :BuildOne x86
if errorlevel 1 exit /b 1
call :BuildOne x64
if errorlevel 1 exit /b 1

echo.
echo BUILD SUCCESSFUL for x86 and x64.
exit /b 0

:BuildOne
set "ARCH=%~1"
set "OUT_DIR=build\%ARCH%"
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

echo.
echo ==== Building %ARCH% ====
cl %COMMON_FLAGS% %SOURCES% %LINK_FLAGS% /MACHINE:%ARCH% /OUT:%OUT_DIR%\d3d9.dll
if errorlevel 1 (
    echo.
    echo BUILD FAILED for %ARCH%.
    exit /b 1
)

echo Output: %OUT_DIR%\d3d9.dll
exit /b 0
