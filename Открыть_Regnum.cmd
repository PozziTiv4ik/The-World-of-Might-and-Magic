@echo off
setlocal
set "REGNUM=%~dp0tools\Regnum\bin\windows\regnum.exe"
if not "%~1"=="" goto args
for /d %%M in ("%~dp012_*") do for /d %%W in ("%%~fM\*") do if exist "%%~fW\world.json" (
  start "" "%REGNUM%" "%%~fW"
  exit /b
)
start "" "%REGNUM%"
exit /b

:args
start "" "%REGNUM%" %*
