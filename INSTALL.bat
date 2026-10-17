@echo off
setlocal EnableExtensions
cd /d "%~dp0"

title Install GBA Repro Save Patcher v0.4.3

echo.
echo ==========================================================
echo   GBA Repro Save Patcher v0.4.3 - Installer
echo ==========================================================
echo.
echo This installs only for your Windows account.
echo No administrator rights are required.
echo.
echo Flash512 support is built directly into the final EXE.
echo No separate gba-flash.exe is required.
echo.

if not exist "GBA-Repro-Save-Patcher.exe" (
  echo Building the application first...
  echo.
  call "BUILD_PORTABLE.bat"
  if errorlevel 1 (
    echo.
    echo Install stopped because the build failed.
    pause
    exit /b 1
  )
)

set "INSTALLDIR=%LOCALAPPDATA%\Programs\GBA Repro Save Patcher"
set "STARTMENU=%APPDATA%\Microsoft\Windows\Start Menu\Programs\GBA Repro Save Patcher"
set "DESKTOP=%USERPROFILE%\Desktop"

mkdir "%INSTALLDIR%" >nul 2>&1
mkdir "%STARTMENU%" >nul 2>&1

copy /y "GBA-Repro-Save-Patcher.exe" "%INSTALLDIR%\GBA-Repro-Save-Patcher.exe" >nul
copy /y "README.md" "%INSTALLDIR%\README.md" >nul
copy /y "THIRD_PARTY_NOTICES.txt" "%INSTALLDIR%\THIRD_PARTY_NOTICES.txt" >nul
copy /y "CHANGELOG.md" "%INSTALLDIR%\CHANGELOG.md" >nul
copy /y "LICENSE.txt" "%INSTALLDIR%\LICENSE.txt" >nul
copy /y "UNINSTALL.bat" "%INSTALLDIR%\UNINSTALL.bat" >nul

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$ws=New-Object -ComObject WScript.Shell;" ^
  "$s=$ws.CreateShortcut('%STARTMENU%\GBA Repro Save Patcher.lnk');" ^
  "$s.TargetPath='%INSTALLDIR%\GBA-Repro-Save-Patcher.exe';" ^
  "$s.WorkingDirectory='%INSTALLDIR%';" ^
  "$s.IconLocation='%INSTALLDIR%\GBA-Repro-Save-Patcher.exe,0';" ^
  "$s.Description='Patch GBA ROM save routines for repro cartridge hardware';" ^
  "$s.Save();" ^
  "$u=$ws.CreateShortcut('%STARTMENU%\Uninstall GBA Repro Save Patcher.lnk');" ^
  "$u.TargetPath='%INSTALLDIR%\UNINSTALL.bat';" ^
  "$u.WorkingDirectory='%INSTALLDIR%';" ^
  "$u.Save();" ^
  "$d=$ws.CreateShortcut('%DESKTOP%\GBA Repro Save Patcher.lnk');" ^
  "$d.TargetPath='%INSTALLDIR%\GBA-Repro-Save-Patcher.exe';" ^
  "$d.WorkingDirectory='%INSTALLDIR%';" ^
  "$d.IconLocation='%INSTALLDIR%\GBA-Repro-Save-Patcher.exe,0';" ^
  "$d.Description='Patch GBA ROM save routines for repro cartridge hardware';" ^
  "$d.Save();"

echo.
echo ==========================================================
echo   INSTALL COMPLETE
echo ==========================================================
echo.
echo Launching...
start "" "%INSTALLDIR%\GBA-Repro-Save-Patcher.exe"
exit /b 0
