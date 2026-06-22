@echo off
REM See COPYING.txt for license details.
REM
REM flash_to_sd.bat — Stage a new firmware build, the OUI database, and the
REM WiFi WPA dictionary wordlist onto the M1 SD card.
REM
REM Usage (run from the repo root):
REM
REM     scripts\flash_to_sd.bat E:                  rem stage everything
REM     scripts\flash_to_sd.bat E: --no-build       rem skip the auto-`make` step
REM     scripts\flash_to_sd.bat E: --no-oui         rem skip OUI DB
REM     scripts\flash_to_sd.bat E: --no-wordlist    rem skip wifi wordlist
REM     scripts\flash_to_sd.bat E: --no-irdb        rem skip Flipper IR database
REM     scripts\flash_to_sd.bat                     rem prompts for drive letter
REM
REM Multiple flags may be combined in any order.
REM
REM What it does:
REM   1. If artifacts\MonstaTek_M1_v0800_wCRC.bin is missing or older than any
REM      source file under m1_csrc/ / Esp_spi_at/ / Core/ / cmake/, runs
REM      `make` inside WSL to rebuild. Pass --no-build to skip this check.
REM   2. (Optional) Builds oui.bin in the repo root if missing.
REM   3. (Optional) Builds wifi_wordlist.txt in the repo root if missing.
REM   4. (Optional) Builds the Flipper IR database under ir_db_out/ if missing.
REM   5. Copies the firmware to <DRIVE>\, oui.bin and wifi_wordlist.txt to
REM      <DRIVE>\databases\, and the IR db to <DRIVE>\INFRARED\.
REM   6. Lists what's now on the card.
REM
REM On the M1:  Menu -> Firmware Update -> select MonstaTek_M1_v0800_wCRC.bin

setlocal enableextensions enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "REPO_ROOT=%SCRIPT_DIR%.."
set "FW_SRC=%REPO_ROOT%\artifacts\MonstaTek_M1_v0800_wCRC.bin"
set "OUI_SRC=%REPO_ROOT%\oui.bin"
set "WORDLIST_SRC=%REPO_ROOT%\wifi_wordlist.txt"
set "IRDB_SRC=%REPO_ROOT%\ir_db_out\INFRARED"
set "SKIP_OUI=0"
set "SKIP_WORDLIST=0"
set "SKIP_IRDB=0"
set "SKIP_BUILD=0"
set "DRIVE="

REM --- Parse arguments in any order ---
:argloop
if "%~1"=="" goto args_done
if /I "%~1"=="--no-oui"      ( set "SKIP_OUI=1"      & shift & goto argloop )
if /I "%~1"=="--no-wordlist" ( set "SKIP_WORDLIST=1" & shift & goto argloop )
if /I "%~1"=="--no-irdb"     ( set "SKIP_IRDB=1"     & shift & goto argloop )
if /I "%~1"=="--no-build"    ( set "SKIP_BUILD=1"    & shift & goto argloop )
if "%DRIVE%"=="" ( set "DRIVE=%~1" & shift & goto argloop )
echo WARN: unrecognised argument "%~1"
shift
goto argloop
:args_done

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

REM --- Sanity-check the firmware artifact; rebuild via WSL `make` if needed.
REM   "Needed" = artifact missing, or any source file under m1_csrc/ /
REM   Esp_spi_at/ / cmake/ / Core/ is newer than the artifact. Skipped if
REM   --no-build was passed.

set "REBUILD=0"
if not exist "%FW_SRC%" set "REBUILD=1"
if "%SKIP_BUILD%"=="1" set "REBUILD=0"

if "%REBUILD%"=="0" if "%SKIP_BUILD%"=="0" if exist "%FW_SRC%" (
    REM PowerShell: any source file newer than the artifact?
    for /f "delims=" %%i in ('powershell -NoProfile -Command ^
        "$art = Get-Item -LiteralPath '%FW_SRC%' -ErrorAction SilentlyContinue;" ^
        "if (-not $art) { 'rebuild'; exit }" ^
        "$dirs = 'm1_csrc','Esp_spi_at\examples\at_spi_master\spi\stm32\main','Core\Src','Core\Inc','cmake\m1_01';" ^
        "$newer = $false;" ^
        "foreach ($d in $dirs) {" ^
        "  $p = Join-Path '%REPO_ROOT%' $d;" ^
        "  if (Test-Path $p) {" ^
        "    Get-ChildItem -LiteralPath $p -Recurse -File -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -gt $art.LastWriteTime } | ForEach-Object { $newer = $true } }" ^
        "}" ^
        "if ($newer) { 'rebuild' } else { 'ok' }"') do (
        if "%%i"=="rebuild" set "REBUILD=1"
    )
)

if "%REBUILD%"=="1" (
    echo === artifact missing or stale; running `make` via WSL ===
    where wsl >nul 2>&1
    if errorlevel 1 (
        echo ERROR: WSL is not on PATH. Build manually in your build env:
        echo         make
        echo or re-run with --no-build to flash an existing artifact.
        exit /b 1
    )
    pushd "%REPO_ROOT%" >nul
    wsl make
    set "MAKE_RC=!errorlevel!"
    popd >nul
    if not "!MAKE_RC!"=="0" (
        echo ERROR: `make` failed ^(rc=!MAKE_RC!^). Run it manually to see the build output.
        exit /b 1
    )
)

if not exist "%FW_SRC%" (
    echo ERROR: %FW_SRC% still not present.
    echo Build the firmware first: open a WSL shell and run `make`, then re-run.
    exit /b 1
)

REM --- Locate python once for any host-side build steps ---
set "PYTHON_AVAILABLE=0"
where python >nul 2>&1
if not errorlevel 1 set "PYTHON_AVAILABLE=1"

REM --- Optionally build the OUI DB if missing ---
if "%SKIP_OUI%"=="0" (
    if not exist "%OUI_SRC%" (
        echo oui.bin not found; building via scripts\build_oui_db.py ...
        if "!PYTHON_AVAILABLE!"=="0" (
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

REM --- Optionally build the WiFi wordlist if missing ---
if "%SKIP_WORDLIST%"=="0" (
    if not exist "%WORDLIST_SRC%" (
        echo wifi_wordlist.txt not found; building via scripts\build_wifi_wordlist.py ...
        if "!PYTHON_AVAILABLE!"=="0" (
            echo ERROR: python is not on PATH. Install Python 3 or pass --no-wordlist.
            exit /b 1
        )
        pushd "%REPO_ROOT%" >nul
        python "%SCRIPT_DIR%build_wifi_wordlist.py" --out wifi_wordlist.txt
        set "PY_RC=!errorlevel!"
        popd >nul
        if not "!PY_RC!"=="0" (
            echo ERROR: build_wifi_wordlist.py failed ^(rc=!PY_RC!^).
            exit /b 1
        )
    )
)

REM --- Optionally build the Flipper IR DB if missing ---
if "%SKIP_IRDB%"=="0" (
    if not exist "%IRDB_SRC%\db\tv.ir" (
        echo IR database not found; building via scripts\fetch_flipper_irdb.py ...
        if "!PYTHON_AVAILABLE!"=="0" (
            echo ERROR: python is not on PATH. Install Python 3 or pass --no-irdb.
            exit /b 1
        )
        pushd "%REPO_ROOT%" >nul
        python "%SCRIPT_DIR%fetch_flipper_irdb.py" --out ir_db_out
        set "PY_RC=!errorlevel!"
        popd >nul
        if not "!PY_RC!"=="0" (
            echo ERROR: fetch_flipper_irdb.py failed ^(rc=!PY_RC!^).
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

REM --- Ensure the databases directory exists if we're staging anything into it ---
if "%SKIP_OUI%"=="0"      ( if not exist "%DRIVE%\databases\" mkdir "%DRIVE%\databases" 2>nul )
if "%SKIP_WORDLIST%"=="0" ( if not exist "%DRIVE%\databases\" mkdir "%DRIVE%\databases" 2>nul )

REM --- Stage the OUI DB ---
if "%SKIP_OUI%"=="0" (
    copy /Y "%OUI_SRC%" "%DRIVE%\databases\oui.bin" >nul
    if errorlevel 1 (
        echo ERROR: failed to copy oui.bin to %DRIVE%\databases\.
        exit /b 1
    )
    echo   OUI DB:    %DRIVE%\databases\oui.bin
) else (
    echo   OUI DB:    skipped ^(--no-oui^)
)

REM --- Stage the WiFi wordlist ---
if "%SKIP_WORDLIST%"=="0" (
    copy /Y "%WORDLIST_SRC%" "%DRIVE%\databases\wifi_wordlist.txt" >nul
    if errorlevel 1 (
        echo ERROR: failed to copy wifi_wordlist.txt to %DRIVE%\databases\.
        exit /b 1
    )
    echo   wordlist:  %DRIVE%\databases\wifi_wordlist.txt
) else (
    echo   wordlist:  skipped ^(--no-wordlist^)
)

REM --- Stage the Flipper IR DB ---
if "%SKIP_IRDB%"=="0" (
    if not exist "%DRIVE%\INFRARED\db\" mkdir "%DRIVE%\INFRARED\db" 2>nul
    copy /Y "%IRDB_SRC%\db\tv.ir"        "%DRIVE%\INFRARED\db\" >nul
    copy /Y "%IRDB_SRC%\db\audio.ir"     "%DRIVE%\INFRARED\db\" >nul
    copy /Y "%IRDB_SRC%\db\projector.ir" "%DRIVE%\INFRARED\db\" >nul
    copy /Y "%IRDB_SRC%\db\ac.ir"        "%DRIVE%\INFRARED\db\" >nul
    if errorlevel 1 (
        echo ERROR: failed to copy IR database to %DRIVE%\INFRARED\db\.
        exit /b 1
    )
    REM Also stage per-model files for the Find My Remote discovery flow.
    if exist "%IRDB_SRC%\browse\" (
        if not exist "%DRIVE%\INFRARED\browse\" mkdir "%DRIVE%\INFRARED\browse" 2>nul
        xcopy /Y /E /I /Q "%IRDB_SRC%\browse" "%DRIVE%\INFRARED\browse" >nul
        if errorlevel 1 (
            echo ERROR: failed to copy IR browse tree to %DRIVE%\INFRARED\browse\.
            exit /b 1
        )
        echo   IR db:     %DRIVE%\INFRARED\db\{tv,audio,projector,ac}.ir + browse\
    ) else (
        echo   IR db:     %DRIVE%\INFRARED\db\{tv,audio,projector,ac}.ir
    )
) else (
    echo   IR db:     skipped ^(--no-irdb^)
)

REM --- Show what we wrote ---
echo.
echo === Contents on %DRIVE% ===
dir /B "%DRIVE%\MonstaTek_M1_v0800_wCRC.bin" 2>nul
if "%SKIP_OUI%"=="0"      dir /B "%DRIVE%\databases\oui.bin" 2>nul
if "%SKIP_WORDLIST%"=="0" dir /B "%DRIVE%\databases\wifi_wordlist.txt" 2>nul
if "%SKIP_IRDB%"=="0"     dir /B "%DRIVE%\INFRARED\db\*.ir" 2>nul

echo.
echo Done. Eject the SD card and pop it into the M1.
echo Then on the device:  Menu -^> Firmware Update -^> MonstaTek_M1_v0800_wCRC.bin
endlocal
exit /b 0
