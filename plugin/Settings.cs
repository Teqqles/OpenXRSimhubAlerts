// plugin/Settings.cs
using OpenXRSimHubAlerts.Shared;

namespace OpenXRSimHubAlerts.Plugin {
  public class Settings {
    public byte Shape = 1;          // bar
    public byte RadarShape = 0;     // 0 car (vertical rect), 1 arrow
    public byte FlagCorner = 0;
    public bool EnableFlags = true;
    public bool EnableRadar = true;
    public bool DemoMode = false;   // preview overlay with synthetic data (plugin-only)
    public float ScaleL = 1f;
    public float ScaleR = 1f;
    public float ScaleFlag = 1f;
    public float ScaleRadar = 1f;
    public float FlagOpacity = 1f;      // 0..1
    public float RadarMaxOpacity = 1f;  // 0..1 ceiling; closeness scales up to this
    public float PosLx = -0.9f;
    public float PosLy = 0f;
    public float PosRx = 0.9f;
    public float PosRy = 0f;
    public float PosFlagx = 0.8f;
    public float PosFlagy = 0.8f;
    public float RadarRange = 80f;
    public uint[] ColorOverride = new uint[8];

    public Config ToConfig() => new Config {
      Shape = Shape,
      RadarShape = RadarShape,
      FlagCorner = FlagCorner,
      EnableFlags = (byte)(EnableFlags ? 1 : 0),
      EnableRadar = (byte)(EnableRadar ? 1 : 0),
      ScaleL = ScaleL,
      ScaleR = ScaleR,
      ScaleFlag = ScaleFlag,
      ScaleRadar = ScaleRadar,
      FlagOpacity = FlagOpacity,
      RadarMaxOpacity = RadarMaxOpacity,
      PosL = new Vec2 { X = PosLx, Y = PosLy },
      PosR = new Vec2 { X = PosRx, Y = PosRy },
      PosFlag = new Vec2 { X = PosFlagx, Y = PosFlagy },
      RadarRange = RadarRange,
      ColorOverride = (uint[])ColorOverride.Clone()
    };
  }
}
