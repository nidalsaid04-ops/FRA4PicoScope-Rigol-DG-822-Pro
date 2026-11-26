@echo off
rem Builds RigolDG822.dll for both 32-bit (Win32) and 64-bit (x64).
rem   build.cmd            -> Release
rem   build.cmd Debug      -> Debug
rem Output: bin\Win32\<Config>\RigolDG822.dll and bin\x64\<Config>\RigolDG822.dll
rem Optional: set VisaRoot=<folder containing Include, lib, Lib_x64> before running.

setlocal
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: Visual Studio with "Desktop development with C++" is not installed.
    exit /b 1
)

set "MSBUILD="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set "MSBUILD=%%i"
if not defined MSBUILD (
    echo ERROR: MSBuild.exe not found.
    exit /b 1
)

set "SLN=%~dp0RigolSignalGenerator.sln"

for %%P in (x86 x64) do (
    echo.
    echo === Building %CONFIG% ^| %%P ===
    "%MSBUILD%" "%SLN%" /nologo /verbosity:minimal /p:Configuration=%CONFIG% /p:Platform=%%P /t:Build
    if errorlevel 1 (
        echo ERROR: %CONFIG% ^| %%P build failed.
        exit /b 1
    )
)

echo.
echo Done:
echo   32-bit: %~dp0bin\Win32\%CONFIG%\RigolDG822.dll
echo   64-bit: %~dp0bin\x64\%CONFIG%\RigolDG822.dll
endlocal
