// plugin/Settings.cs
using System;
using OpenXRSimHubAlerts.Shared;

namespace OpenXRSimHubAlerts.Plugin {
  public class Settings {
    [Limits(0, 4)] public byte Shape = 0;       // bar; must match the Shape combo items
    [Limits(0, 1)] public byte RadarShape = 0;  // 0 car (vertical rect), 1 arrow
    public RefreshMode RefreshMode = RefreshMode.Auto;  // overlay re-render cap
    public bool EnableFlags = true;
    public bool EnableRadar = true;
    public bool DemoMode = false;   // preview overlay with synthetic data (plugin-only)
    public string Headset = "Other"; // preview-only: draws an approximate visible-area mask
    [Limits(0.2, 4)] public float ScaleFlag = 1f;
    [Limits(0.2, 4)] public float ScaleRadar = 1f;
    [Limits(0.1, 1)] public float FlagOpacity = 1f;
    [Limits(0.1, 1)] public float RadarMaxOpacity = 1f;  // ceiling; closeness scales up to this
    [Limits(-1, 1)] public float PosFlagx = 0.5f;   // inside the headset visible area by default
    [Limits(-1, 1)] public float PosFlagy = 0.5f;
    [Limits(20, 200)] public float RadarRange = 80f;
    public bool EnableShiftLights = true;
    public bool ShowUnlitLights = true;  // dim unlit lights; false hides them
    [Limits(3, 20)] public int ShiftLightCount = 10;
    [Limits(0.2, 4)] public float ScaleShift = 1f;
    [Limits(0, 1)] public float ShiftGlow = 0.5f;       // halo size and strength; 0 = no halo
    [Limits(0.1, 1)] public float ShiftOpacity = 1f;
    [Limits(-1, 1)] public float PosShiftx = 0f;         // row centre, from straight ahead
    [Limits(-1, 1)] public float PosShifty = 0.7f;
    public bool EnableAbs = true;
    public bool EnableTc = true;
    public bool EnableDrs = true;
    [Limits(0.2, 4)] public float ScaleAids = 1f;
    [Limits(0.1, 1)] public float AidsOpacity = 1f;
    [Limits(-1, 1)] public float PosAidsx = 0f;          // centre of the three slots, from straight ahead
    [Limits(-1, 1)] public float PosAidsy = -0.55f;
  }

  // Range of a numeric setting: loads and imports clamp to it, sliders use it.
  [AttributeUsage(AttributeTargets.Field)]
  public sealed class LimitsAttribute : Attribute {
    public double Min { get; }
    public double Max { get; }

    public LimitsAttribute(double min, double max) {
      Min = min;
      Max = max;
    }

    public static LimitsAttribute Of(string settingName) =>
      (LimitsAttribute)GetCustomAttribute(typeof(Settings).GetField(settingName), typeof(LimitsAttribute))
        ?? throw new ArgumentException(settingName + " has no [Limits]", nameof(settingName));
  }
}
