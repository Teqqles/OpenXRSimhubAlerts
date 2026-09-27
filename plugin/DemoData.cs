// plugin/DemoData.cs
using System;

namespace OpenXRSimHubAlerts.Plugin {
  // Synthetic telemetry for Demo mode: cycles through every flag and orbits a
  // few radar cars so a user can preview the overlay in-headset without a
  // running sim. Cars sit left, right or behind, never ahead, matching the
  // layer's rule that cars purely ahead are tracked but not drawn.
  // Pure and deterministic in elapsed time so it can be unit tested.
  public static class DemoData {
    static readonly FlagType[] FlagCycle = {
      FlagType.Green, FlagType.Yellow, FlagType.Blue, FlagType.White,
      FlagType.Red, FlagType.Black, FlagType.Meatball
    };
    public const double FlagPeriodSeconds = 1.5;
    public const int DemoCarCount = 3;

    public const double ShiftPeriodSeconds = 4.0;
    const double DemoStartRpm = 5000, DemoRedlineRpm = 7800, DemoMaxRpm = 8000;
    const double IdleRpm = 3000, RampSeconds = 3.0;

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

      // The rear car orbits so its blip sweeps behind <-> left <-> right, showing
      // its true relative bearing rather than sitting parked dead-behind. Bearing
      // is atan2(x, -y); theta stays in the rear hemisphere so the side is only
      // ever left/right/behind (never ahead). Side is derived from that bearing.
      float theta = 1.2f * (float)Math.Sin(t * 0.4);           // rear sweep, radians
      float rx = (float)Math.Sin(theta);
      float ry = -(float)Math.Cos(theta);
      byte behindSide = theta < -0.5f ? (byte)1 : theta > 0.5f ? (byte)2 : (byte)4;

      // Side codes: 1 = left, 2 = right, 4 = behind. Flags bit 0 = closest threat.
      cars[0] = new CarBlip { Rel = new Vec2 { X = -3f,  Y =  osc }, Distance = dLeft,   Side = 1, Flags = 1 };
      cars[1] = new CarBlip { Rel = new Vec2 { X =  3f,  Y = -osc }, Distance = dRight,  Side = 2, Flags = 0 };
      cars[2] = new CarBlip { Rel = new Vec2 { X = 3f * rx, Y = 3f * ry }, Distance = dBehind, Side = behindSide, Flags = 0 };
      return (uint)DemoCarCount;
    }

    // RPM climbs from idle to the redline over three seconds, holds just past it for
    // one second so the blue flash shows, then the next gear starts from idle.
    public static ShiftInput Shift(double t) {
      double phase = t % ShiftPeriodSeconds;
      if (phase < 0) phase += ShiftPeriodSeconds;
      int gear = (int)Math.Floor(t / ShiftPeriodSeconds) % 5;
      if (gear < 0) gear += 5;
      double rpm = phase < RampSeconds
        ? IdleRpm + (DemoRedlineRpm - IdleRpm) * phase / RampSeconds
        : DemoRedlineRpm + 100;
      return new ShiftInput {
        Rpm = rpm, MaxRpm = DemoMaxRpm, StartRpm = DemoStartRpm,
        RedlineRpm = DemoRedlineRpm, Gear = (gear + 1).ToString(),
      };
    }

    public const double AidsPeriodSeconds = 3.0;

    // ABS flickers for 0.6 s, then TC, then DRS is available and then open, so the
    // preview shows every driver aid state each cycle.
    public static DriverAidInput Aids(double t) {
      double phase = t % AidsPeriodSeconds;
      if (phase < 0) phase += AidsPeriodSeconds;
      bool flicker = Math.Sin(t * 60) > 0;
      return new DriverAidInput {
        Abs = phase < 0.6 && flicker,
        Tc = phase >= 0.9 && phase < 1.5 && flicker,
        DrsAvailable = phase >= 1.8,
        DrsOpen = phase >= 2.4,
      };
    }
  }
}
