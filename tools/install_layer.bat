@echo off
REM Registers the OpenXR SimHub Alerts API layer (per-user, HKCU, no admin).
REM Double-click or run from a terminal.
setlocal

REM Use the release layout (layer\ next to tools\) if present, else the local build.
set "LAYER_DIR=%~dp0..\layer"
if not exist "%LAYER_DIR%\OpenXRSimHubAlerts.dll" set "LAYER_DIR=%~dp0..\layer\build\Release"
for %%I in ("%LAYER_DIR%") do set "LAYER_DIR=%%~fI"

if not exist "%LAYER_DIR%\OpenXRSimHubAlerts.dll" (
  echo ERROR: OpenXRSimHubAlerts.dll not found in "%LAYER_DIR%".
  echo Extract the release zip, or build the layer in Release first.
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
