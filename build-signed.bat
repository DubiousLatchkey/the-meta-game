@echo off
setlocal
cd /d "%~dp0"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\build-signed.ps1" %*
exit /b %ERRORLEVEL%
