@echo off
setlocal
call "%~dp0..\build.bat"
if errorlevel 1 exit /b %errorlevel%
"%~dp0..\release\the-meta-game.exe" --interior-graph-tuner
