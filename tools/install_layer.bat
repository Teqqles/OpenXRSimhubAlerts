@echo off
REM Registers the OpenXR SimHub Alerts API layer (per-user, HKCU, no admin).
REM Double-click or run from a terminal. Uses the Release build next to the repo.
setlocal

REM Resolve the Release output dir to an absolute path relative to this script.
for %%I in ("%~dp0..\layer\build\Release") do set "LAYER_DIR=%%~fI"

if not exist "%LAYER_DIR%\OpenXRSimHubAlerts.dll" (
  echo ERROR: OpenXRSimHubAlerts.dll not found in "%LAYER_DIR%".
  echo Build the layer in Release first ^(cmake --build layer\build --config Release^).
  pause
  exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install_layer.ps1" -Dir "%LAYER_DIR%"
if errorlevel 1 (
  echo.
  echo Failed to register the layer.
  pause
  exit /b 1
)

echo.
echo Layer registered from "%LAYER_DIR%".
pause
endlocal
