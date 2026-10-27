@echo off
rem MatrixGame - licensed under GPLv2 or any later version.
rem Guided build: runs tools\build-wizard.ps1 with Windows PowerShell. Use "build.bat -Help" for options.
setlocal EnableExtensions DisableDelayedExpansion
set "MATRIXGAME_WIZARD=%~dp0tools\build-wizard.ps1"
set "MATRIXGAME_POWERSHELL=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%MATRIXGAME_POWERSHELL%" set "MATRIXGAME_POWERSHELL=powershell.exe"
if "%~1"=="/?" goto help
if /I "%~1"=="--help" goto help
if /I "%~1"=="-h" goto help
"%MATRIXGAME_POWERSHELL%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%MATRIXGAME_WIZARD%" %*
exit /b %errorlevel%

:help
"%MATRIXGAME_POWERSHELL%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%MATRIXGAME_WIZARD%" -Help
exit /b %errorlevel%
