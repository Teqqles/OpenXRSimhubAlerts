// plugin/Telemetry.cs
using System;

namespace OpenXRSimHubAlerts.Plugin {
  [Flags] public enum FlagType : byte {
    None=0, Green=1, Yellow=2, Blue=4, White=8, Red=16, Black=32, Meatball=64
  }

  public struct Vec2 { public float X, Y; }

  // A radar car relative to the player. Side: 0 none, 1 left, 2 right, 3 ahead,
  // 4 behind. Flags bit 0: closest car on its side.
  public struct CarBlip {
    public Vec2 Rel;
    public float Distance;
    public byte Side;
    public byte Flags;
  }
}
