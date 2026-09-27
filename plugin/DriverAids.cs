// plugin/DriverAids.cs
namespace OpenXRSimHubAlerts.Plugin {
  public struct DriverAidInput { public bool Abs, Tc, DrsAvailable, DrsOpen; }

  public enum DrsState : byte { Off, Available, Open }

  public struct DriverAidState { public bool Abs, Tc; public DrsState Drs; }

  // ABS and TC switch on and off many times a second while they work, so each
  // stays shown for HoldSeconds after its last active sample and reads as steady.
  public sealed class DriverAids {
    public const double HoldSeconds = 0.2;

    double _absUntil = double.NegativeInfinity, _tcUntil = double.NegativeInfinity;

    public DriverAidState Update(DriverAidInput input, double timeSeconds) {
      if (input.Abs) _absUntil = timeSeconds + HoldSeconds;
      if (input.Tc) _tcUntil = timeSeconds + HoldSeconds;
      return new DriverAidState {
        Abs = timeSeconds < _absUntil,
        Tc = timeSeconds < _tcUntil,
        Drs = input.DrsOpen ? DrsState.Open : input.DrsAvailable ? DrsState.Available : DrsState.Off,
      };
    }
  }
}
