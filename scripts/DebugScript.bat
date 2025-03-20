@echo off
:: Switch to the directory where the batch file is located
cd /d "%~dp0"

:: Check if a parameter was provided (i.e., if a file was dragged onto the batch file)
if "%~1"=="" (
    echo No file was dragged onto the script.
    pause
    exit /b
)

:: Set the first parameter as the file path
set "FilePath=%~1"

:: Output the file path and executable path for debugging
echo Executing with file: "%FilePath%"
echo Using executable: "../x64/Release/loxFlux.exe"

:: Call your executable and pass the file path as an argument
"../x64/Debug/loxFlux.exe" "%FilePath%"

:: Pause to view the execution result
pause