# Registers the OpenXRSimHubAlerts implicit API layer for the current user.
# Adds a DWORD value (name = full path to the manifest, data = 0 -> enabled)
# under HKCU:\Software\Khronos\OpenXR\1\ApiLayers\Implicit.
param(
  [string]$Dir = "layer\build\Debug"
)

$ErrorActionPreference = "Stop"

$manifest = Join-Path (Resolve-Path $Dir) "OpenXRSimHubAlerts.json"
if (-not (Test-Path $manifest)) {
  throw "Manifest not found: $manifest (build the layer first)"
}

$key = "HKCU:\Software\Khronos\OpenXR\1\ApiLayers\Implicit"
if (-not (Test-Path $key)) {
  New-Item -Path $key -Force | Out-Null
}

New-ItemProperty -Path $key -Name $manifest -PropertyType DWord -Value 0 -Force | Out-Null
Write-Host "Registered implicit API layer: $manifest"
