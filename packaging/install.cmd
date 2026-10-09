@echo off
setlocal
set "PP_PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if defined PROCESSOR_ARCHITEW6432 set "PP_PS=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
"%PP_PS%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\install.ps1"
set "PP_RESULT=%ERRORLEVEL%"
echo.
if not "%PP_RESULT%"=="0" echo Installation failed. See the message above.
pause
exit /b %PP_RESULT%
