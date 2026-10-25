@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\diagnose.ps1" %*
pause
