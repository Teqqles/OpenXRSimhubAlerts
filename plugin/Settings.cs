// plugin/Settings.cs
using OpenXRSimHubAlerts.Shared;

namespace OpenXRSimHubAlerts.Plugin {
  public class Settings {
    public byte Shape = 0;          // bar
    public byte RadarShape = 0;     // 0 car (vertical rect), 1 arrow
    public RefreshMode RefreshMode = RefreshMode.Auto;  // overlay re-render cap
    public bool EnableFlags = true;
    public bool EnableRadar = true;
    public bool DemoMode = false;   // preview overlay with synthetic data (plugin-only)
    public string Headset = "Other"; // preview-only: draws an approximate visible-area mask
    public float ScaleFlag = 1f;
    public float ScaleRadar = 1f;
    public float FlagOpacity = 1f;      // 0..1
    public float RadarMaxOpacity = 1f;  // 0..1 ceiling; closeness scales up to this
    public float PosFlagx = 0.5f;   // inside the headset visible area by default
    public float PosFlagy = 0.5f;
    public float RadarRange = 80f;
    public bool EnableShiftLights = true;
    public bool ShowUnlitLights = true;  // dim unlit lights; false hides them
    public int ShiftLightCount = 10;     // 3..20
    public float ScaleShift = 1f;
    public float ShiftGlow = 0.5f;       // 0..1 halo size and strength; 0 = no halo
    public float ShiftOpacity = 1f;      // 0..1
    public float PosShiftx = 0f;         // row centre, from straight ahead
    public float PosShifty = 0.7f;
  }
}
