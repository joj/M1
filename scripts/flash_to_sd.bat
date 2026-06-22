@echo off
REM See COPYING.txt for license details.
REM
REM flash_to_sd.bat — Stage a new firmware build and the OUI database onto the
REM M1 SD card for the on-device updater.
REM
REM Usage (run from the repo root, after `make` has produced artifacts/):
REM
REM     scripts\flash_to_sd.bat E:           rem stage to drive E:
REM     scripts\flash_to_sd.bat E: --no-oui  rem firmware only, skip OUI DB
REM     scripts\flash_to_sd.bat              rem prompts for drive letter
REM
REM What it does:
REM   1. Verifies artifacts\MonstaTek_M1_v0800_wCRC.bin exists.
REM   2. (Optional) Builds oui.bin in the repo root if missing, via the
REM      Python tool scripts\build_oui_db.py — needs network.
REM   3. Copies the firmware to <DRIVE>\ and oui.bin to <DRIVE>\databases\.
REM   4. Lists what's now on the card so you can sanity-check.
REM
REM On the M1:  Menu -> Firmware Update -> select MonstaTek_M1_v0800_wCRC.bin

setlocal enableextensions enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "REPO_ROOT=%SCRIPT_DIR%.."
set "FW_SRC=%REPO_ROOT%\artifacts\MonstaTek_M1_v0800_wCRC.bin"
set "OUI_SRC=%REPO_ROOT%\oui.bin"
set "SKIP_OUI=0"

REM --- Parse arguments ---
set "DRIVE=%~1"
if /I "%~2"=="--no-oui" set "SKIP_OUI=1"
if /I "%~1"=="--no-oui" (
    set "SKIP_OUI=1"
    set "DRIVE=%~2"
)

if "%DRIVE%"=="" (
    set /p "DRIVE=Enter SD card drive letter (e.g. E:): "
)
if "%DRIVE%"=="" (
    echo ERROR: no drive specified.
    exit /b 1
)

REM Normalise: accept "E", "E:", "E:\" -> "E:"
set "DRIVE=%DRIVE:\=%"
if not "%DRIVE:~-1%"==":" set "DRIVE=%DRIVE%:"

if not exist "%DRIVE%\" (
    echo ERROR: drive %DRIVE% is not present.
    exit /b 1
)

REM --- Sanity-check the firmware artifact ---
if not exist "%FW_SRC%" (
    echo ERROR: %FW_SRC% not found.
    echo Build the firmware first: open a WSL shell in the repo root and run `make`.
    exit /b 1
)

REM --- Optionally build the OUI DB if missing ---
if "%SKIP_OUI%"=="0" (
    if not exist "%OUI_SRC%" (
        echo oui.bin not found; building via scripts\build_oui_db.py ...
        where python >nul 2>&1
        if errorlevel 1 (
            echo ERROR: python is not on PATH. Install Python 3 or pass --no-oui.
            exit /b 1
        )
        pushd "%REPO_ROOT%" >nul
        python "%SCRIPT_DIR%build_oui_db.py" --out oui.bin
        set "PY_RC=!errorlevel!"
        popd >nul
        if not "!PY_RC!"=="0" (
            echo ERROR: build_oui_db.py failed ^(rc=!PY_RC!^).
            exit /b 1
        )
    )
)

REM --- Stage the firmware ---
echo.
echo === Staging firmware to %DRIVE%\ ===
copy /Y "%FW_SRC%" "%DRIVE%\" >nul
if errorlevel 1 (
    echo ERROR: failed to copy firmware to %DRIVE%\.
    exit /b 1
)
echo   firmware:  %DRIVE%\MonstaTek_M1_v0800_wCRC.bin

REM --- Stage the OUI DB ---
if "%SKIP_OUI%"=="0" (
    if not exist "%DRIVE%\databases\" (
        mkdir "%DRIVE%\databases" 2>nul
    )
    copy /Y "%OUI_SRC%" "%DRIVE%\databases\oui.bin" >nul
    if errorlevel 1 (
        echo ERROR: failed to copy oui.bin to %DRIVE%\databases\.
        exit /b 1
    )
    echo   OUI DB:    %DRIVE%\databases\oui.bin
) else (
    echo   OUI DB:    skipped ^(--no-oui^)
)

REM --- Show what we wrote ---
echo.
echo === Contents on %DRIVE% ===
dir /B "%DRIVE%\MonstaTek_M1_v0800_wCRC.bin" 2>nul
if "%SKIP_OUI%"=="0" dir /B "%DRIVE%\databases\oui.bin" 2>nul

echo.
echo Done. Eject the SD card and pop it into the M1.
echo Then on the device:  Menu -^> Firmware Update -^> MonstaTek_M1_v0800_wCRC.bin
endlocal
exit /b 0
