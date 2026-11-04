@echo off
cd /d "%~dp0"
python verify_submission.py
if errorlevel 1 exit /b 1
pause
