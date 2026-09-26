@echo off
REM Unregisters the OpenXR SimHub Alerts API layer (per-user, HKCU).
setlocal

REM Use the release layout (layer\ next to tools\) if present, else the local build.
set "LAYER_DIR=%~dp0..\layer"
if not exist "%LAYER_DIR%\OpenXRSimHubAlerts.dll" set "LAYER_DIR=%~dp0..\layer\build\Release"
for %%I in ("%LAYER_DIR%") do set "LAYER_DIR=%%~fI"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0uninstall_layer.ps1" -Dir "%LAYER_DIR%"
if errorlevel 1 (
  echo.
  echo Failed to unregister the layer.
  pause
  exit /b 1
)

echo.
echo Layer unregistered.
pause
endlocal
