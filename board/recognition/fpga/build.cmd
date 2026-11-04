@echo off
setlocal
cd /d "%~dp0"
call "D:\Efinity\bin\setup.bat"
"D:\Efinity\python311\bin\python.exe" build.py
exit /b %ERRORLEVEL%
