@echo off
REM Unregisters the OpenXR SimHub Alerts API layer (per-user, HKCU).
setlocal

for %%I in ("%~dp0..\layer\build\Release") do set "LAYER_DIR=%%~fI"

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
