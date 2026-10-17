@echo off
cd /d "%~dp0"
if not exist "GBA-Repro-Save-Patcher.exe" (
  echo The app has not been built yet.
  echo Run BUILD_PORTABLE.bat or INSTALL.bat first.
  pause
  exit /b 1
)
start "" "GBA-Repro-Save-Patcher.exe"
