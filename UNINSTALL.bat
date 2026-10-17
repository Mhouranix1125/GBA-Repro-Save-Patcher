@echo off
setlocal EnableExtensions
set "INSTALLDIR=%LOCALAPPDATA%\Programs\GBA Repro Save Patcher"
set "STARTMENU=%APPDATA%\Microsoft\Windows\Start Menu\Programs\GBA Repro Save Patcher"
set "DESKTOP=%USERPROFILE%\Desktop"

echo.
echo GBA Repro Save Patcher - Uninstall
echo.
choice /M "Remove the application and its shortcuts"
if errorlevel 2 exit /b 0

del /q "%DESKTOP%\GBA Repro Save Patcher.lnk" >nul 2>&1
rmdir /s /q "%STARTMENU%" >nul 2>&1
start "" /b cmd /c "timeout /t 2 /nobreak >nul & rmdir /s /q ""%INSTALLDIR%"""
exit /b 0
