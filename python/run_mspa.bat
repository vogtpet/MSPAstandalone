@echo off
rem Run MSPA, creating/repairing the project's Python environment if needed.
rem Usage: run_mspa.bat -i input.tif -o output.tif
setlocal EnableExtensions
set "HERE=%~dp0"
set "REQUIREMENTS=%HERE%requirements.txt"

if not defined NUMBA_THREADING_LAYER (
    if defined MSPA_NUMBA_LAYER (
        set "NUMBA_THREADING_LAYER=%MSPA_NUMBA_LAYER%"
    ) else (
        set "NUMBA_THREADING_LAYER=workqueue"
    )
)

if defined MSPA_PYTHON (
    if exist "%MSPA_PYTHON%" (
        "%MSPA_PYTHON%" "%HERE%run_mspa.py" %*
        exit /b %errorlevel%
    )
    echo ERROR: MSPA_PYTHON not found: %MSPA_PYTHON% 1>&2
    exit /b 2
)

if exist "%HERE%.venv\Scripts\python.exe" (
    set "PY=%HERE%.venv\Scripts\python.exe"
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

echo No usable Python environment found. Creating "%HERE%.venv" ...
set "PY="
where py >nul 2>&1
if not errorlevel 1 (
    py -3.12 -m venv "%HERE%.venv" >nul 2>&1
    if exist "%HERE%.venv\Scripts\python.exe" set "PY=%HERE%.venv\Scripts\python.exe"
)
if not defined PY (
    py -3.11 -m venv "%HERE%.venv" >nul 2>&1
    if exist "%HERE%.venv\Scripts\python.exe" set "PY=%HERE%.venv\Scripts\python.exe"
)
if not defined PY (
    py -3.10 -m venv "%HERE%.venv" >nul 2>&1
    if exist "%HERE%.venv\Scripts\python.exe" set "PY=%HERE%.venv\Scripts\python.exe"
)
if not defined PY (
    python -m venv "%HERE%.venv"
    if exist "%HERE%.venv\Scripts\python.exe" set "PY=%HERE%.venv\Scripts\python.exe"
)
if not defined PY (
    echo ERROR: could not create a virtual environment. 1>&2
    echo Install Python 3.10-3.13 and rerun. 1>&2
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
        exit /b 2
    )
)

"%PY%" "%HERE%run_mspa.py" %*
exit /b %errorlevel%
