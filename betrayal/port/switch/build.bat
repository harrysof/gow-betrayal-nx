@echo off
rem Builds gow_switch.nro with devkitPro's devkitA64/libnx toolchain.
rem Usage: build.bat [clean]
setlocal
cd /d "%~dp0"

set "DKP=%DEVKITPRO%"
if "%DKP%"=="" if exist "%USERPROFILE%\devkitPro\msys2\msys2_shell.cmd" set "DKP=%USERPROFILE%\devkitPro"
if "%DKP%"=="" if exist "C:\devkitPro\msys2\msys2_shell.cmd" set "DKP=C:\devkitPro"
if "%DKP%"=="" if exist "%ProgramFiles%\devkitPro\msys2\msys2_shell.cmd" set "DKP=%ProgramFiles%\devkitPro"

if not exist "%DKP%\msys2\msys2_shell.cmd" (
  echo Could not find devkitPro. Install it from https://github.com/devkitPro/installer/releases
  echo or set DEVKITPRO to the installation folder.
  exit /b 1
)

set "JOBS=%NUMBER_OF_PROCESSORS%"
if "%JOBS%"=="" set "JOBS=4"
set "MAKECMD=make -j%JOBS%"
if /i "%1"=="clean" set "MAKECMD=make clean"

call "%DKP%\msys2\msys2_shell.cmd" -defterm -no-start -here -c "%MAKECMD%"
if errorlevel 1 (
  echo Build failed.
  exit /b 1
)

if /i not "%1"=="clean" (
  echo.
  echo Built %~dp0gow_switch.nro
)
endlocal
