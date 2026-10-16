@echo off
rem ====================================================================
rem  fetch_discord_sdk.bat
rem  Stage the Discord Social SDK binaries into the defolddiscordsdk Defold
rem  extension so the native SDK can be loaded at build time. This is the
rem  Windows twin of fetch_discord_sdk.sh.
rem
rem  Usage:
rem    tools\fetch_discord_sdk.bat [PATH] [--src PATH] [--dest PATH] [--mobile]
rem
rem  PATH is either:
rem    (a) an already extracted SDK folder (default: repo root
rem        discord_social_sdk), or
rem    (b) a .zip archive of the SDK, unpacked to a temporary directory
rem        first with PowerShell Expand-Archive.
rem
rem  Options:
rem    --src PATH   SDK source: folder or .zip; overrides positional PATH
rem    --dest PATH  extension destination folder (default: repo defolddiscordsdk)
rem    --mobile     also stage arm64-android and arm64-ios artifacts when
rem                 present (future phases; default off)
rem    -h, --help   this help text
rem
rem  Exit codes:
rem    0  at least one core Discord partner SDK binary was staged
rem    1  staging finished but no core partner binary is present
rem    2  usage error (bad arguments, missing source, unsafe destination)
rem
rem  The script is idempotent: rerunning refreshes the staged files.
rem ====================================================================
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "REPO_ROOT=%%~fI"
for %%F in ("%~f0") do set "PROGRAM=%%~nxF"

if "%~1"=="" goto parse_done
goto parse
:parse_loop
if "%~1"=="" goto parse_done
:parse
set "ARG=%~1"
if /I "%ARG%"=="--mobile" (
    set "MOBILE=1"
    shift
    goto parse_loop
)
if /I "%ARG%"=="--src" (
    if "%~2"=="" (
        echo error: --src needs a value 1>&2
        exit /b 2
    )
    set "SRC=%~2"
    shift
    shift
    goto parse_loop
)
if /I "%ARG%"=="--dest" (
    if "%~2"=="" (
        echo error: --dest needs a value 1>&2
        exit /b 2
    )
    set "DEST=%~2"
    shift
    shift
    goto parse_loop
)
if /I "%ARG%"=="-h" goto show_help
if /I "%ARG%"=="--help" goto show_help
if /I "%ARG%"=="--" (
    shift
    goto parse_loop
)
if "%ARG%"=="" goto parse_done
if "%ARG:~0,1%"=="-" (
    echo error: unknown option "%ARG%" 1>&2
    call :usage
    exit /b 2
)
if not defined SRC (
    set "SRC=%~1"
    shift
    goto parse_loop
)
echo error: unexpected argument "%ARG%" 1>&2
call :usage
exit /b 2

:show_help
call :usage
exit /b 0

:parse_done
if not defined SRC set "SRC=%REPO_ROOT%\discord_social_sdk"
if not defined DEST set "DEST=%REPO_ROOT%\defolddiscordsdk"
if "%SRC:~-1%"=="\" set "SRC=%SRC:~0,-1%"
if "%DEST:~-1%"=="\" set "DEST=%DEST:~0,-1%"
set /a CORE_COUNTED=0
set /a MISSING_COUNTED=0
set /a FAILED_COUNTED=0
set "MAC_HINT=0"

goto main

:usage
echo Usage: %PROGRAM% [PATH] [--src PATH] [--dest PATH] [--mobile]
echo.
echo Stage Discord Social SDK binaries into the defolddiscordsdk extension.
echo.
echo   PATH         extracted SDK folder or a .zip archive
echo                default folder: %REPO_ROOT%\discord_social_sdk
echo   --src PATH   SDK source, folder or .zip; overrides the positional arg
echo   --dest PATH  destination extension folder, default %REPO_ROOT%\defolddiscordsdk
echo   --mobile     also stage arm64-android and arm64-ios artifacts when present
echo   -h, --help   show this help
echo.
echo Exits 0 only when at least one core Discord partner SDK binary was staged.
goto :eof

rem ------------------------------------------------------------------
rem Helpers
rem ------------------------------------------------------------------

rem :log STATUS SUBDIR FILE SOURCE  - one aligned summary row
:log
set "PAD_STAT=%~1                    "
set "PAD_STAT=%PAD_STAT:~0,10%"
set "PAD_PLAT=%~2                        "
set "PAD_PLAT=%PAD_PLAT:~0,15%"
set "PAD_FILE=%~3                                      "
set "PAD_FILE=%PAD_FILE:~0,38%"
echo  %PAD_STAT% %PAD_PLAT% %PAD_FILE% %~4
if /I "%~1"=="MISS" set /a MISSING_COUNTED+=1
if /I "%~1"=="FAIL" set /a FAILED_COUNTED+=1
goto :eof

rem :stage_file SUBDIR NAME DIR1 [DIR2 ...]
rem Copies the first matching file into %DEST%\res\SUBDIR.
:stage_file
set "F_SUB=%~1"
set "F_NAME=%~2"
shift
shift
:find_file_loop
if "%~1"=="" goto find_file_done
if exist "%SDK_ROOT%\%~1\%F_NAME%" (
    set "F_DIR=%~1"
    goto copy_file
)
shift
goto find_file_loop
:find_file_done
call :log MISS "%F_SUB%" "%F_NAME%" "not found in archive"
exit /b 1
:copy_file
if not exist "%DEST%\res\%F_SUB%" mkdir "%DEST%\res\%F_SUB%"
copy /Y "%SDK_ROOT%\%F_DIR%\%F_NAME%" "%DEST%\res\%F_SUB%\" >nul
if errorlevel 1 (
    call :log FAIL "%F_SUB%" "%F_NAME%" "copy failed"
    exit /b 1
)
call :log OK "%F_SUB%" "%F_NAME%" "%F_DIR%\%F_NAME%"
goto :eof

rem :stage_kefs SUBDIR DIR1 [DIR2 ...]
rem Copies all *.kef krisp model files from the first dir that carries any.
:stage_kefs
set "F_SUB=%~1"
shift
:find_kef_loop
if "%~1"=="" goto find_kef_done
if exist "%SDK_ROOT%\%~1\*.kef" (
    set "F_DIR=%~1"
    goto copy_kefs
)
shift
goto find_kef_loop
:find_kef_done
call :log MISS "%F_SUB%" "*.kef" "not found in archive"
exit /b 1
:copy_kefs
if not exist "%DEST%\res\%F_SUB%" mkdir "%DEST%\res\%F_SUB%"
for %%K in ("%SDK_ROOT%\%F_DIR%\*.kef") do (
    copy /Y "%%~fK" "%DEST%\res\%F_SUB%\" >nul
    if errorlevel 1 (
        call :log FAIL "%F_SUB%" "%%~nxK" "copy failed"
        exit /b 1
    )
    call :log OK "%F_SUB%" "%%~nxK" "%F_DIR%"
)
goto :eof

rem :stage_dir SUBDIR NAME DIR1 [DIR2 ...]
rem Recursively copies a directory such as an .xcframework.
:stage_dir
set "F_SUB=%~1"
set "F_NAME=%~2"
shift
shift
:find_dir_loop
if "%~1"=="" goto find_dir_done
if exist "%SDK_ROOT%\%~1\%F_NAME%" (
    set "F_DIR=%~1"
    goto copy_dir_target
)
shift
goto find_dir_loop
:find_dir_done
call :log MISS "%F_SUB%" "%F_NAME%" "not found in archive"
exit /b 1
:copy_dir_target
if exist "%DEST%\res\%F_SUB%\%F_NAME%" rmdir /s /q "%DEST%\res\%F_SUB%\%F_NAME%" >nul 2>&1
if not exist "%DEST%\res\%F_SUB%" mkdir "%DEST%\res\%F_SUB%"
robocopy "%SDK_ROOT%\%F_DIR%\%F_NAME%" "%DEST%\res\%F_SUB%\%F_NAME%" /E /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 (
    call :log FAIL "%F_SUB%" "%F_NAME%" "robocopy failed"
    exit /b 1
)
call :log OK "%F_SUB%" "%F_NAME%" "%F_DIR%\%F_NAME"
goto :eof

:main
rem ------------------------------------------------------------------
rem Source preparation: unpack a zip, then resolve the SDK root
rem ------------------------------------------------------------------
set "EXTRACT="
set "LEXT=%SRC:~-4%"
if /I "%LEXT%"==".zip" goto unzip_step
goto resolve_root

:unzip_step
if not exist "%SRC%" (
    echo error: zip archive not found: %SRC% 1>&2
    exit /b 2
)
set "EXTRACT=%TEMP%\defolddiscordsdk_%RANDOM%"
echo Unpacking %SRC% -^> %EXTRACT%
powershell -NoProfile -ExecutionPolicy Bypass -Command "Expand-Archive -LiteralPath '%SRC%' -DestinationPath '%EXTRACT%' -Force"
if errorlevel 1 (
    echo error: failed to unpack %SRC% 1>&2
    exit /b 2
)
set "SRC=%EXTRACT%"

:resolve_root
if not exist "%SRC%\*" (
    echo error: source folder not found: %SRC% 1>&2
    call :usage
    exit /b 2
)
set "SDK_ROOT=%SRC%"
if exist "%SDK_ROOT%\lib" goto have_root
if exist "%SDK_ROOT%\bin" goto have_root
for /D %%D in ("%SDK_ROOT%\*") do (
    if exist "%%~fD\lib" (
        set "SDK_ROOT=%%~fD"
        goto have_root
    )
    if exist "%%~fD\bin" (
        set "SDK_ROOT=%%~fD"
        goto have_root
    )
)
echo error: no bin or lib folder layout under: %SDK_ROOT% 1>&2
echo        point --src at the extracted Discord Social SDK archive. 1>&2
exit /b 2

:have_root
rem Normalize an explicit destination guard and refuse unsafe targets.
if "%DEST:~-1%"=="\" set "DEST=%DEST:~0,-1%"
if /I "%DEST%"=="%REPO_ROOT%" (
    echo error: unsafe destination "%DEST%"; pass --dest pointing at the extension folder 1>&2
    exit /b 2
)
if /I "%DEST%"=="%REPO_ROOT%\tools" (
    echo error: unsafe destination "%DEST%"; pass --dest pointing at the extension folder 1>&2
    exit /b 2
)
if /I "%CD%"=="%DEST%" (
    echo error: refuse to run from inside the destination folder "%DEST%" 1>&2
    echo        run from the repository root instead. 1>&2
    exit /b 2
)

if not exist "%DEST%\res" mkdir "%DEST%\res"
echo.
echo SDK root:     %SDK_ROOT%
echo Staging into: %DEST%\res
echo.
echo Staged artifacts, paths relative to %DEST%\res:
call :log PLATFORM FILE ---- SOURCE
echo.

rem ------------------------------------------------------------------
rem Desktop platforms
rem ------------------------------------------------------------------
rem Windows x86_64: dlls and krisp models; bin\release preferred, bin\debug fallback.
call :stage_file "x86_64-win32" "discord_partner_sdk.dll" "bin\release" "bin\debug" "bin\release\arm64" "bin\debug\arm64"
call :stage_file "x86_64-win32" "discord_krisp.dll" "bin\release" "bin\debug"
call :stage_kefs "x86_64-win32" "bin\release" "bin\debug"

rem macOS: loose dylibs at the archive lib root, release preferred.
call :stage_file "x86_64-osx" "libdiscord_partner_sdk.dylib" "lib\release" "lib\debug" "bin\release\arm64" "bin\debug\arm64"
if errorlevel 1 set "MAC_HINT=1"
call :stage_file "x86_64-osx" "libdiscord_krisp.dylib" "lib\release" "lib\debug"
call :stage_kefs "x86_64-osx" "lib\release" "lib\debug"

rem Linux: partner library only. No krisp library ships for Linux in the
rem current archive; the absence is handled gracefully.
call :stage_file "x86_64-linux" "libdiscord_partner_sdk.so" "lib\release" "lib\debug"

rem ------------------------------------------------------------------
rem Mobile, opt-in via --mobile
rem ------------------------------------------------------------------
if "%MOBILE%"=="1" (
    call :stage_file "arm64-android" "discord_partner_sdk.aar" "lib\release" "lib\debug"
    call :stage_dir "arm64-ios" "discord_partner_sdk.xcframework" "lib\release" "lib\debug"
)

rem ------------------------------------------------------------------
rem Summary
rem ------------------------------------------------------------------
echo.
if "%MAC_HINT%"=="1" (
    echo note: no loose macOS dylib was found in this archive.
    echo       If your copy only ships macOS slices inside an XCFramework, copy:
    echo         SDK_ROOT\discord_partner_sdk.xcframework\macos-x\discord_partner_sdk.framework\discord_partner_sdk
    echo       into: %DEST%\res\x86_64-osx\libdiscord_partner_sdk.dylib
    echo       Staging continues with the files that do exist.
    echo.
)

rem Final verdict based on what actually landed on disk.
set "CORE_COUNTED=0"
if exist "%DEST%\res\x86_64-win32\discord_partner_sdk.dll" set /a CORE_COUNTED+=1
if exist "%DEST%\res\x86_64-osx\libdiscord_partner_sdk.dylib" set /a CORE_COUNTED+=1
if exist "%DEST%\res\x86_64-linux\libdiscord_partner_sdk.so" set /a CORE_COUNTED+=1
if "%MOBILE%"=="1" (
    if exist "%DEST%\res\arm64-android\discord_partner_sdk.aar" set /a CORE_COUNTED+=1
    if exist "%DEST%\res\arm64-ios\discord_partner_sdk.xcframework" set /a CORE_COUNTED+=1
)

echo Summary: %CORE_COUNTED% core partner staging hits, %MISSING_COUNTED% missing, %FAILED_COUNTED% failed.

if not "%EXTRACT%"=="" (
    rmdir /s /q "%EXTRACT%" >nul 2>&1
)

if not "%CORE_COUNTED%"=="0" (
    echo The Discord partner SDK core is staged. Binaries stay untracked, see .gitignore.
    exit /b 0
)
echo No core Discord partner SDK binary staged.
echo Check that the source really is the extracted Discord Social SDK archive.
exit /b 1
