@echo off
setlocal enabledelayedexpansion

rem URKit build launcher.
rem   build.bat                     -> interactive menu
rem   build.bat msvc release        -> non-interactive
rem   build.bat clang debug clean   -> non-interactive + wipe build dir
rem   build.bat package             -> MSVC release build + installer + portable zip + checksums
rem   build.bat package clean       -> same, from a clean build
rem   build.bat package patch       -> bump the version first (patch, minor, major or x.y.z)
rem   build.bat package clean patch -> both

set "COMPILER=%~1"
set "CONFIG=%~2"
set "EXTRA=%~3"

if /i "%COMPILER%"=="package" (
    set "PS_ARGS="
    for %%A in (%~2 %~3) do (
        if /i "%%A"=="clean" (set "PS_ARGS=!PS_ARGS! -Clean") else (set "PS_ARGS=!PS_ARGS! -Bump %%A")
    )
    goto :package_run
)

if "%COMPILER%"=="" (
    echo === URKit Build ===
    echo.
    echo Action:
    echo   [1] Build
    echo   [2] Release package ^(installer, portable zip, checksums^)
    choice /c 12 /n /m "Select action: "
    if errorlevel 2 (
        set "EXTRA="
        echo.
        goto :package_menu
    )
    echo.
    echo Compiler:
    echo   [1] MSVC  ^(cl.exe^)
    echo   [2] Clang
    choice /c 12 /n /m "Select compiler: "
    if errorlevel 2 (set "COMPILER=clang") else (set "COMPILER=msvc")
    echo.
)

if "%CONFIG%"=="" (
    echo Configuration:
    echo   [1] Release
    echo   [2] Debug
    choice /c 12 /n /m "Select configuration: "
    if errorlevel 2 (set "CONFIG=debug") else (set "CONFIG=release")
    echo.
)

if "%EXTRA%"=="" (
    choice /c yn /n /m "Clean the existing build output first? (y/n): "
    if errorlevel 2 (set "EXTRA=") else (set "EXTRA=clean")
    echo.
)

set "PS_ARGS=-Compiler %COMPILER% -Config %CONFIG%"
if /i "%EXTRA%"=="clean" set "PS_ARGS=%PS_ARGS% -Clean"

echo Running: powershell -File build.ps1 %PS_ARGS%
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %PS_ARGS%
set "RC=%ERRORLEVEL%"
goto :done

:package_menu
choice /c yn /n /m "Clean the existing build output first? (y/n): "
if errorlevel 2 (set "EXTRA=") else (set "EXTRA=clean")
echo.

:package
set "PS_ARGS="
if /i "%EXTRA%"=="clean" set "PS_ARGS=-Clean"

:package_run
echo Running: powershell -File installer\package-release.ps1 %PS_ARGS%
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0installer\package-release.ps1" %PS_ARGS%
set "RC=%ERRORLEVEL%"

:done
echo.
if "%RC%"=="0" (
    echo Finished successfully.
) else (
    echo Failed with exit code %RC%.
)

exit /b %RC%
