@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\start.ps1" -Role Host %*
pause
