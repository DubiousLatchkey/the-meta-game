@echo off
setlocal
cd /d "%~dp0"

set "MSVC_ROOT=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC"
set "SDK_ROOT=C:\Program Files (x86)\Windows Kits\10"

for /f "delims=" %%D in ('dir /b /ad /o-n "%MSVC_ROOT%" 2^>nul') do if not defined MSVC_VERSION set "MSVC_VERSION=%%D"
for /f "delims=" %%D in ('dir /b /ad /o-n "%SDK_ROOT%\Lib" 2^>nul') do if not defined SDK_VERSION set "SDK_VERSION=%%D"

if not defined MSVC_VERSION (
    echo The MSVC C++ build tools were not found.
    exit /b 1
)
if not defined SDK_VERSION (
    echo The Windows SDK was not found.
    exit /b 1
)

set "MSVC=%MSVC_ROOT%\%MSVC_VERSION%"
set "PATH=%MSVC%\bin\Hostx64\x64;%PATH%"
set "INCLUDE=%MSVC%\include;%SDK_ROOT%\Include\%SDK_VERSION%\ucrt;%SDK_ROOT%\Include\%SDK_VERSION%\shared;%SDK_ROOT%\Include\%SDK_VERSION%\um;%SDK_ROOT%\Include\%SDK_VERSION%\winrt;%SDK_ROOT%\Include\%SDK_VERSION%\cppwinrt"
set "LIB=%MSVC%\lib\x64;%SDK_ROOT%\Lib\%SDK_VERSION%\ucrt\x64;%SDK_ROOT%\Lib\%SDK_VERSION%\um\x64"

nmake /nologo /f Makefile
