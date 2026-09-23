@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0upgrade-ocr-v4.ps1" %*
if errorlevel 1 pause
endlocal
