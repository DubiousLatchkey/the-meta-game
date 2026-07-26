@echo off
setlocal
cd /d "%~dp0"

set "MSVC_ROOT=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC"
set "SDK_ROOT=C:\Program Files (x86)\Windows Kits\10"
set "TOOLCHAIN_CACHE=%LOCALAPPDATA%\the-meta-game-toolchain.env"

if exist "%TOOLCHAIN_CACHE%" (
    for /f "tokens=1,* delims==" %%A in (%TOOLCHAIN_CACHE%) do set "%%A=%%B"
)
if defined MSVC_VERSION if not exist "%MSVC_ROOT%\%MSVC_VERSION%\bin\Hostx64\x64\cl.exe" set "MSVC_VERSION="
if defined SDK_VERSION if not exist "%SDK_ROOT%\Lib\%SDK_VERSION%\um\x64\kernel32.lib" set "SDK_VERSION="

if not defined MSVC_VERSION for /f "delims=" %%D in ('dir /b /ad /o-n "%MSVC_ROOT%" 2^>nul') do if not defined MSVC_VERSION set "MSVC_VERSION=%%D"
if not defined SDK_VERSION for /f "delims=" %%D in ('dir /b /ad /o-n "%SDK_ROOT%\Lib" 2^>nul') do if not defined SDK_VERSION set "SDK_VERSION=%%D"

if not defined MSVC_VERSION (
    echo The MSVC C++ build tools were not found.
    exit /b 1
)
if not defined SDK_VERSION (
    echo The Windows SDK was not found.
    exit /b 1
)

> "%TOOLCHAIN_CACHE%" echo MSVC_VERSION=%MSVC_VERSION%
>> "%TOOLCHAIN_CACHE%" echo SDK_VERSION=%SDK_VERSION%

set "MSVC=%MSVC_ROOT%\%MSVC_VERSION%"
set "PATH=%MSVC%\bin\Hostx64\x64;%SDK_ROOT%\bin\%SDK_VERSION%\x64;%PATH%"
set "INCLUDE=%MSVC%\include;%SDK_ROOT%\Include\%SDK_VERSION%\ucrt;%SDK_ROOT%\Include\%SDK_VERSION%\shared;%SDK_ROOT%\Include\%SDK_VERSION%\um;%SDK_ROOT%\Include\%SDK_VERSION%\winrt;%SDK_ROOT%\Include\%SDK_VERSION%\cppwinrt"
set "LIB=%MSVC%\lib\x64;%SDK_ROOT%\Lib\%SDK_VERSION%\ucrt\x64;%SDK_ROOT%\Lib\%SDK_VERSION%\um\x64"

nmake /nologo /f Makefile %*
