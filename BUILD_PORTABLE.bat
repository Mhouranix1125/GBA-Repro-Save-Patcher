@echo off
setlocal EnableExtensions
cd /d "%~dp0"

title Build GBA Repro Save Patcher v0.4.3

echo.
echo ==========================================================
echo   GBA Repro Save Patcher v0.4.3 - Integrated Build
echo ==========================================================
echo.

set "GPP=C:\msys64\ucrt64\bin\g++.exe"
set "WINDRES=C:\msys64\ucrt64\bin\windres.exe"
set "ARMGCC=C:\msys64\ucrt64\bin\arm-none-eabi-gcc.exe"
set "ARMOBJCOPY=C:\msys64\ucrt64\bin\arm-none-eabi-objcopy.exe"

if not exist "%GPP%" (
  echo ERROR: UCRT64 g++ was not found.
  echo Open MSYS2 UCRT64 and run:
  echo   pacman -S mingw-w64-ucrt-x86_64-gcc
  echo.
  pause
  exit /b 1
)

if not exist "%WINDRES%" (
  echo ERROR: windres.exe was not found.
  pause
  exit /b 1
)

if not exist "%ARMGCC%" (
  echo ERROR: ARM embedded GCC was not found.
  echo Open MSYS2 UCRT64 and run:
  echo   pacman -S mingw-w64-ucrt-x86_64-arm-none-eabi-toolchain
  echo.
  pause
  exit /b 1
)

if not exist "%ARMOBJCOPY%" (
  echo ERROR: arm-none-eabi-objcopy.exe was not found.
  pause
  exit /b 1
)

echo [1/5] Compiling embedded GBA Flash512 payload...
"%ARMGCC%" -mcpu=arm7tdmi -nostartfiles -nodefaultlibs -mthumb -fPIE -Os -fno-toplevel-reorder ^
  "third_party\gba-flash-patcher\payload.c" ^
  -T "third_party\gba-flash-patcher\payload.ld" ^
  -o "src\payload.elf"

if errorlevel 1 goto :fail

echo [2/5] Converting ARM payload to raw binary...
"%ARMOBJCOPY%" -O binary "src\payload.elf" "src\payload.bin"
if errorlevel 1 goto :fail

echo [3/5] Embedding payload inside the Windows application...
powershell -NoProfile -ExecutionPolicy Bypass -File "src\embed_payload.ps1" ^
  "src\payload.bin" "src\payload_bin.cpp"
if errorlevel 1 goto :fail

echo [4/5] Compiling Windows resources...
"%WINDRES%" "src\app.rc" -O coff -o "src\app_res.o"
if errorlevel 1 goto :fail

echo [5/5] Building single standalone Windows EXE...
"%GPP%" -std=c++17 -O2 -municode -mwindows ^
  -static -static-libgcc -static-libstdc++ ^
  "src\main.cpp" "src\payload_bin.cpp" "src\app_res.o" ^
  -o "GBA-Repro-Save-Patcher.exe" ^
  -lcomctl32 -lcomdlg32 -lshell32 -lole32 -luuid -lgdi32

if errorlevel 1 (
  echo.
  echo Static build failed. Retrying a normal UCRT64 build...
  "%GPP%" -std=c++17 -O2 -municode -mwindows ^
    "src\main.cpp" "src\payload_bin.cpp" "src\app_res.o" ^
    -o "GBA-Repro-Save-Patcher.exe" ^
    -lcomctl32 -lcomdlg32 -lshell32 -lole32 -luuid -lgdi32
)

if errorlevel 1 goto :fail

del /q "src\app_res.o" >nul 2>&1
del /q "src\payload.elf" >nul 2>&1
del /q "src\payload.bin" >nul 2>&1
del /q "src\payload_bin.cpp" >nul 2>&1

echo.
echo ==========================================================
echo   BUILD SUCCESSFUL
echo ==========================================================
echo.
echo Created:
echo   %cd%\GBA-Repro-Save-Patcher.exe
echo.
echo Flash512 is compiled INTO the EXE.
echo No gba-flash.exe helper is needed anymore.
echo.
pause
exit /b 0

:fail
echo.
echo ==========================================================
echo   BUILD FAILED
echo ==========================================================
echo.
echo Copy the compiler error above and send it to ChatGPT.
echo.
pause
exit /b 1
