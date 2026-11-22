@echo off
rem Double-click me: runs the read-only PC check and keeps the window open.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0check_pc.ps1"
pause
