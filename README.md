# OpenXR SimHub Alerts

An OpenXR API layer that draws race flags and a traffic radar inside your VR headset,
fed live from SimHub. It works with DX11, DX12 and Vulkan OpenXR titles and needs no
support from the game.

- **Flags:** shows the current flag (green, yellow, blue, white, black, meatball) as a
  full-height bar at the outer edge of each eye, or as a rectangle, square, circle or
  triangle you can scale and place. The meatball flag draws as a black flag with an
  orange disc.
- **Traffic radar:** marks cars beside and behind you on a ring around each lens. A
  car on your left shows in the left eye, a car on your right in the right eye, and a
  car behind in both. Closer cars draw brighter and more opaque. The layer tracks cars
  ahead but does not draw them, since you can already see them.

## How it works

```
SimHub plugin (.NET/WPF)  --writes-->  shared memory (seqlock)  --reads-->  OpenXR API layer (C++ DLL)
  turns telemetry into shapes            DataBlock: list of elements           hooks xrEndFrame, adds overlay quads
```

Every telemetry tick, the plugin's `OverlayComposer` turns the active flag and the
radar cars into a list of elements (rectangles, ellipses and triangles with a
position, size, colour, target eye and priority) and writes them as a `DataBlock`.
The layer reads it once per frame, draws the elements into its own swapchain and
appends two head-locked quad layers (one per eye) to the game's frame. The layer
knows nothing about flags or radar, and the settings preview draws the same elements.

`shared/shm_contract.h` defines the shared-memory layout. `shared/ShmContract.cs`
mirrors it for C#, and layout tests on both sides pin the sizes and offsets so the two
cannot drift apart.

## Requirements

- Windows 10 or 11, x64
- CMake 3.21 or newer and MSVC (Visual Studio 2022 or its Build Tools)
- Vulkan SDK, with `VULKAN_SDK` set (the Vulkan backend needs its headers)
- A .NET SDK that can target .NET Framework 4.8
- SimHub, with the `SIMHUB` environment variable set to its install folder, for
  example `C:\Program Files (x86)\SimHub`

## Build

GitHub Actions builds and tests every pull request. On `main`, a `feat` commit
releases a new minor version and a `fix` commit a new patch version; other commit
types release nothing.

Layer (C++ DLL and native tests):

```bash
cmake -S layer -B layer/build
cmake --build layer/build --config Release
ctest --test-dir layer/build -C Release --output-on-failure
```

This produces `layer/build/Release/OpenXRSimHubAlerts.dll` with its manifest,
`OpenXRSimHubAlerts.json`, next to it.

Plugin (SimHub plugin and C# tests):

```bash
dotnet build plugin -c Release
dotnet test plugin.tests -c Release
```

This produces `plugin/bin/Release/net48/OpenXRSimHubAlerts.Plugin.dll`.

## Install

Download `OpenXRSimHubAlerts-Setup-vX.Y.Z.exe` from
[Releases](https://github.com/Teqqles/OpenXRSimhubAlerts/releases) and run it. It
asks for admin rights once, then:

- installs the layer to `C:\Program Files\OpenXRSimHubAlerts` and registers it for
  every user
- copies the plugin into your SimHub folder, which it finds from SimHub's settings or
  asks you for
- removes any other registration of the layer, including ones made by the scripts
  below, so only one copy loads

Close SimHub before you run it. Start SimHub afterwards and enable
**OpenXR SimHub Alerts** when it asks.

To update, run a newer setup; it replaces the installed files. To remove everything,
uninstall **OpenXR SimHub Alerts** from Windows Settings > Apps.

### Manual install

1. **Download.** Get the latest zip from
   [Releases](https://github.com/Teqqles/OpenXRSimhubAlerts/releases) and extract it
   to a permanent folder. The layer registration points at that folder, so moving it
   later means registering again.

2. **Register the layer.** Run `tools\install_layer.bat` (double-click works). It
   registers the DLL in the zip's `layer` folder, or `layer\build\Release` in a
   source build, for your user account under
   `HKCU\Software\Khronos\OpenXR\1\ApiLayers\Implicit`. It needs no admin rights.
   Run `tools\uninstall_layer.bat` to remove it.

   To register a different folder, call the PowerShell script directly:

   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\install_layer.ps1 -Dir <folder containing the DLL>
   ```

3. **Install the plugin.** Copy `plugin\OpenXRSimHubAlerts.Plugin.dll` into your SimHub
   folder, start SimHub and enable **OpenXR SimHub Alerts**.

## Settings

Open the **OpenXR SimHub Alerts** entry in SimHub's left menu. Changes apply live.

| Setting | What it does |
|---------|--------------|
| Enable flags / Enable radar | Turn each overlay on or off |
| Demo mode | Feeds synthetic flags and cars so you can check the overlay without a running sim |
| Shape | Flag shape: bar, rectangle, square, circle or triangle. Non-bar shapes share the same visual area |
| Radar shape | Car marker or arrow pointing at the car |
| Radar range | Distance in metres at which cars appear |
| Radar scale, Radar max opacity | Size of radar markers and the opacity of the closest car |
| Flag scale, Flag opacity | Size and opacity of the flag shape |
| Flag position X / Y | Where non-bar flag shapes sit. X mirrors to the outer edge of each eye |
| Headset | Dims the preview outside your headset's approximate visible area. Preview only |
| Overlay refresh rate | How often the layer redraws the overlay: Auto, Unlimited, or a fixed 60, 30, 15, 10, 5 or 1 fps |

The settings page shows an animated stereo preview of both eyes over a cockpit
background, so you can tune the overlay without putting the headset on.

### Overlay refresh rate

The layer redraws the overlay only on frames the refresh rate allows. On the frames in
between, the headset keeps showing the last drawn overlay, so a lower rate saves GPU
time without flicker. Flag and radar changes then appear at the next redraw, up to one
second later at 1 fps.

**Auto** (the default) redraws every frame while your system holds the headset's
refresh rate. If more than 10% of frames miss in any one-second window, it drops to
30 fps, then 20, then 10. After 10 seconds of stable frames it steps back up one level
at a time. The layer logs each step to `%LOCALAPPDATA%\OpenXRSimHubAlerts\layer.log`.

## Disabling the layer

Set the environment variable `DISABLE_OPENXR_SIMHUB_ALERTS` to any value and the
OpenXR loader skips the layer for that process, with no need to unregister it.

The layer also steps aside on its own. If SimHub is not running, the shared memory is
missing or stale, or any overlay step fails, it submits the game's frame unchanged.

## Known limitations

- SimHub exposes no red-flag property, so the red flag never shows.
- When a game reports only spline position for other cars, the radar estimates their
  lateral offset from track width.

## Status

The native and C# test suites pass and all three graphics backends build. These still
need checking on real hardware:

- Overlay placement, legibility and colour (sRGB vs UNORM swapchain formats)
- The DX12 resource-state transitions (COMMON to RENDER_TARGET and back)
- The Vulkan render pass final layout
- A smoke test per API: a DX11 title such as iRacing or ACC, a DX12 title, a Vulkan
  title (or `hello_xr -g d3d11|d3d12|vulkan2`), and pass-through with SimHub closed

## License

[MIT](LICENSE)
