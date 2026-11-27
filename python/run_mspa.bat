@echo off
rem Run MSPA, creating/repairing the project's Python environment if needed.
rem Usage: run_mspa.bat -i input.tif -o output.tif
setlocal EnableExtensions
set "HERE=%~dp0"
set "VENV=%HERE%.venv"
set "REQUIREMENTS=%HERE%requirements.txt"

rem The bundled workqueue layer is always available and avoids TBB-discovery
rem warnings.  Override with MSPA_NUMBA_LAYER=tbb or omp.
if not defined NUMBA_THREADING_LAYER (
    if defined MSPA_NUMBA_LAYER (
        set "NUMBA_THREADING_LAYER=%MSPA_NUMBA_LAYER%"
    ) else (
        set "NUMBA_THREADING_LAYER=workqueue"
    )
)

rem An explicit interpreter (full path) always wins.
if defined MSPA_PYTHON (
    if exist "%MSPA_PYTHON%" (
        "%MSPA_PYTHON%" "%HERE%run_mspa.py" %*
        exit /b %errorlevel%
    )
    echo ERROR: MSPA_PYTHON not found: %MSPA_PYTHON% 1>&2
    exit /b 2
)

rem Reuse an existing project environment.
if exist "%VENV%\Scripts\python.exe" (
    set "PY=%VENV%\Scripts\python.exe"
    goto check
)
if exist "%HERE%venv\Scripts\python.exe" (
    set "PY=%HERE%venv\Scripts\python.exe"
    goto check
)
if exist "%HERE%..\.venv\Scripts\python.exe" (
    set "PY=%HERE%..\.venv\Scripts\python.exe"
    goto check
)

echo No usable Python environment found. Creating "%VENV%" ...
set "PY="

rem Native Windows-on-ARM installations use a -arm64 suffix; try them before
rem the x64/emulated ones so Numba and NumPy run natively.
call :try_exe "%LOCALAPPDATA%\Programs\Python\Python313-arm64\python.exe"
call :try_exe "%LOCALAPPDATA%\Programs\Python\Python312-arm64\python.exe"
call :try_exe "%LOCALAPPDATA%\Programs\Python\Python311-arm64\python.exe"
call :try_exe "%LOCALAPPDATA%\Programs\Python\Python310-arm64\python.exe"

rem Standard per-user installs (python.org default).
call :try_exe "%LOCALAPPDATA%\Programs\Python\Python313\python.exe"
call :try_exe "%LOCALAPPDATA%\Programs\Python\Python312\python.exe"
call :try_exe "%LOCALAPPDATA%\Programs\Python\Python311\python.exe"
call :try_exe "%LOCALAPPDATA%\Programs\Python\Python310\python.exe"

rem The py launcher, newest supported version first.  This also finds
rem installs that are registered but not on PATH.
if not defined PY call :try_py -3.13
if not defined PY call :try_py -3.12
if not defined PY call :try_py -3.11
if not defined PY call :try_py -3.10

rem All-users installs.
call :try_exe "%ProgramFiles%\Python313\python.exe"
call :try_exe "%ProgramFiles%\Python312\python.exe"
call :try_exe "%ProgramFiles%\Python311\python.exe"
call :try_exe "%ProgramFiles%\Python310\python.exe"

rem Last resort: scan the standard trees for any Python 3.x directory,
rem including names such as "Python313-arm64" and "Python313-32".
if not defined PY call :scan_python "%LOCALAPPDATA%\Programs\Python"
if not defined PY call :scan_python "%ProgramFiles%"
if not defined PY call :scan_python "%ProgramFiles(x86)%"

rem Finally try commands on PATH.  The Microsoft Store alias in WindowsApps is
rem rejected, because it only opens the Store instead of running Python.
if not defined PY call :try_cmd python3.13
if not defined PY call :try_cmd python3
if not defined PY call :try_cmd python

if not defined PY (
    echo ERROR: could not find a usable Python 3.10-3.13 installation. 1>&2
    echo Install Python 3.10-3.13 ^(3.12 or newer for native Windows ARM64^) 1>&2
    echo and enable "Add Python to PATH" in the installer. 1>&2
    echo. 1>&2
    echo Already installed?  List the registered interpreters with: 1>&2
    echo     py -0p 1>&2
    echo or pass the full path explicitly, for example: 1>&2
    echo     set MSPA_PYTHON=%%LOCALAPPDATA%%\Programs\Python\Python313-arm64\python.exe 1>&2
    echo     run_mspa.bat -i input.tif -o output.tif 1>&2
    echo. 1>&2
    echo If "python" opens the Microsoft Store, disable the App Execution 1>&2
    echo Alias under Settings ^> Apps ^> Advanced app settings. 1>&2
    exit /b 2
)

:check
"%PY%" -c "import numpy, scipy, numba, PIL" >nul 2>&1
if errorlevel 1 (
    echo Installing MSPA Python requirements into "%PY%" ...
    "%PY%" -m pip install --upgrade pip
    "%PY%" -m pip install -r "%REQUIREMENTS%"
    if errorlevel 1 (
        echo ERROR: dependency installation failed. 1>&2
        echo Remove "%VENV%" and rerun, or use Python 3.10-3.13. 1>&2
        echo On Windows ARM64 the Numba 0.68 wheels are used automatically. 1>&2
        exit /b 2
    )
)

"%PY%" "%HERE%run_mspa.py" %*
exit /b %errorlevel%

rem --- helpers -------------------------------------------------------------

:try_exe
rem Create the venv with a concrete python.exe path (%~1), if supported.
if defined PY goto :eof
if not exist "%~1" goto :eof
rem Accept Python 3.10-3.13.  Native Windows ARM64 needs 3.12+ because Numba
rem only publishes ARM64 wheels for those versions.
"%~1" -c "import sys, platform; ok = (3, 10) <= sys.version_info[:2] <= (3, 13) and (platform.machine() != 'ARM64' or sys.version_info[:2] >= (3, 12)); raise SystemExit(0 if ok else 1)" >nul 2>&1
if errorlevel 1 goto :eof
"%~1" -m venv "%VENV%" >nul 2>&1
if exist "%VENV%\Scripts\python.exe" set "PY=%VENV%\Scripts\python.exe"
goto :eof

:try_py
rem Create the venv through the py launcher: %~1 is a version selector.
if defined PY goto :eof
where py >nul 2>&1
if errorlevel 1 goto :eof
py %~1 -m venv "%VENV%" >nul 2>&1
if exist "%VENV%\Scripts\python.exe" set "PY=%VENV%\Scripts\python.exe"
goto :eof

:scan_python
rem Try every Python3* directory under a standard installation root.
if defined PY goto :eof
if not exist "%~1" goto :eof
for /d %%D in ("%~1\Python3*") do call :try_exe "%%~D\python.exe"
goto :eof

:try_cmd
rem Try a command from PATH.  The Microsoft Store alias stub fails the version
rem check in :try_exe, so no separate path filtering is needed.
if defined PY goto :eof
for /f "delims=" %%P in ('where "%~1" 2^>nul') do call :try_exe "%%P"
goto :eof
