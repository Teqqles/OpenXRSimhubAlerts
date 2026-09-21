// shared/ShmContract.cs
using System;
using System.Runtime.InteropServices;

namespace OpenXRSimHubAlerts.Shared {
  public static class ShmContract {
    public const string Name = "OpenXRSimHubAlerts";
    public const uint Version = 2;
    public const int MaxCars = 64;
  }

  [Flags] public enum FlagType : byte {
    None=0, Green=1, Yellow=2, Blue=4, White=8, Red=16, Black=32, Meatball=64
  }

  [StructLayout(LayoutKind.Sequential, Pack=4)]
  public struct Vec2 { public float X, Y; }

  [StructLayout(LayoutKind.Sequential, Pack=4)]
  public struct CarBlip {
    public Vec2 Rel; public float Distance; public byte Side; public byte Flags;
    public byte Pad0, Pad1;
  }

  [StructLayout(LayoutKind.Sequential, Pack=4)]
  public struct Config {
    public byte Shape, RadarShape, FlagCorner, EnableFlags, EnableRadar;
    public byte Pad0, Pad1, Pad2;
    public float ScaleL, ScaleR, ScaleFlag;
    public float ScaleRadar;       // radar blip size multiplier
    public float FlagOpacity;      // 0..1 flag alpha
    public float RadarMaxOpacity;  // 0..1 radar alpha ceiling; closeness scales up to this
    public Vec2 PosL, PosR, PosFlag;
    public float RadarRange;
    [MarshalAs(UnmanagedType.ByValArray, SizeConst=8)] public uint[] ColorOverride;
  }

  [StructLayout(LayoutKind.Sequential, Pack=4)]
  public struct DataBlock {
    public uint Version, Seq;
    public byte Connected, ActiveFlags, Pad0, Pad1;
    public uint CarCount;
    [MarshalAs(UnmanagedType.ByValArray, SizeConst=ShmContract.MaxCars)] public CarBlip[] Cars;
    public Config Config;
  }
}
