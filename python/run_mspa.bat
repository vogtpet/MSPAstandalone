@echo off
rem Run the MSPA Python CLI with the project's virtual environment.
rem Usage: run_mspa.bat -i input.tif -o output.tif
setlocal
set "HERE=%~dp0"

if defined MSPA_PYTHON (
    if exist "%MSPA_PYTHON%" (
        "%MSPA_PYTHON%" "%HERE%run_mspa.py" %*
        exit /b %errorlevel%
    )
)

if exist "%HERE%.venv\Scripts\python.exe" (
    "%HERE%.venv\Scripts\python.exe" "%HERE%run_mspa.py" %*
    exit /b %errorlevel%
)
if exist "%HERE%venv\Scripts\python.exe" (
    "%HERE%venv\Scripts\python.exe" "%HERE%run_mspa.py" %*
    exit /b %errorlevel%
)
if exist "%HERE%..\.venv\Scripts\python.exe" (
    "%HERE%..\.venv\Scripts\python.exe" "%HERE%run_mspa.py" %*
    exit /b %errorlevel%
)

python "%HERE%run_mspa.py" %*
exit /b %errorlevel%
