@echo off
rem Docker build wrapper for Windows (uses Git Bash)
set BASH=%ProgramFiles%\Git\bin\bash.exe
if exist "%BASH%" (
  "%BASH%" "%~dp0docker-build.sh"
) else (
  echo Git Bash not found at "%BASH%".
  pause
)
