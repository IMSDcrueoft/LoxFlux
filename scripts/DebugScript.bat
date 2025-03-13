@echo off
:: Check if a parameter was provided (i.e., if a file was dragged onto the batch file)
if "%~1"=="" (
    echo No file was dragged onto the script.
    pause
    exit /b
)

:: Set the first parameter as the file path
set "FilePath=%~1"

:: Output the file path (optional)
echo Executing with file: %FilePath%

:: Call your executable and pass the file path as an argument
:: Note: Adjust the relative or absolute path of the executable according to your actual situation
"../x64/Debug/loxFlux.exe" "%FilePath%"

:: Pause to view the execution result
pause