# Unregisters the OpenXRSimHubAlerts implicit API layer for the current user.
# Removes the DWORD value (name = full path to the manifest) under
# HKCU:\Software\Khronos\OpenXR\1\ApiLayers\Implicit.
param(
  [string]$Dir = "layer\build\Debug"
)

$ErrorActionPreference = "Stop"

$manifest = Join-Path (Resolve-Path $Dir) "OpenXRSimHubAlerts.json"

$key = "HKCU:\Software\Khronos\OpenXR\1\ApiLayers\Implicit"
if (Test-Path $key) {
  $prop = Get-ItemProperty -Path $key -Name $manifest -ErrorAction SilentlyContinue
  if ($prop) {
    Remove-ItemProperty -Path $key -Name $manifest
    Write-Host "Unregistered implicit API layer: $manifest"
  } else {
    Write-Host "No registration found for: $manifest"
  }
} else {
  Write-Host "Implicit API layer key does not exist; nothing to do."
}
