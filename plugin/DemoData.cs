// plugin/DemoData.cs
using System;
using OpenXRSimHubAlerts.Shared;

namespace OpenXRSimHubAlerts.Plugin {
  // Synthetic telemetry for Demo mode: cycles through every flag and orbits a
  // few radar cars so a user can preview the overlay in-headset without a
  // running sim. Cars are placed left / right / behind only -- never ahead --
  // matching the hard rule that cars purely ahead are tracked but not drawn.
  // Pure and deterministic in elapsed time so it can be unit tested.
  public static class DemoData {
    static readonly FlagType[] FlagCycle = {
      FlagType.Green, FlagType.Yellow, FlagType.Blue, FlagType.White,
      FlagType.Red, FlagType.Black, FlagType.Meatball
    };
    public const double FlagPeriodSeconds = 1.5;
    public const int DemoCarCount = 3;

    // Fills cars[] with the demo blips and returns the flag bits to show at time
    // t (seconds). Returns the number of cars written.
    public static uint Fill(double t, CarBlip[] cars, out byte activeFlags) {
      int fi = (int)(t / FlagPeriodSeconds) % FlagCycle.Length;
      if (fi < 0) fi += FlagCycle.Length;
      activeFlags = (byte)FlagCycle[fi];

      float osc = (float)Math.Sin(t);
      float dLeft   = 6f  + 3f * osc;                          // closest -> threat
      float dRight  = 10f + 4f * (float)Math.Sin(t * 0.7);
      float dBehind = 14f + 5f * (float)Math.Sin(t * 0.5);

      // Side codes: 1 = left, 2 = right, 4 = behind. Flags bit 0 = closest threat.
      cars[0] = new CarBlip { Rel = new Vec2 { X = -3f,  Y =  osc }, Distance = dLeft,   Side = 1, Flags = 1 };
      cars[1] = new CarBlip { Rel = new Vec2 { X =  3f,  Y = -osc }, Distance = dRight,  Side = 2, Flags = 0 };
      cars[2] = new CarBlip { Rel = new Vec2 { X =  0.5f, Y = -6f }, Distance = dBehind, Side = 4, Flags = 0 };
      return (uint)DemoCarCount;
    }
  }
}
