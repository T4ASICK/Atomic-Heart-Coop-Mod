@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\start.ps1" -Role Join -Transport Steam %*
pause
