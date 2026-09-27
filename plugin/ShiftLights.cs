// plugin/ShiftLights.cs
using System;

namespace OpenXRSimHubAlerts.Plugin {
  // Engine state for the shift light row, as read from the sim.
  public struct ShiftInput {
    public double Rpm, MaxRpm, StartRpm, RedlineRpm;
    public string Gear;
  }

  // What the row shows: Lit lights from the left, or all lights flashing blue.
  public struct ShiftState {
    public int Lit;
    public bool Flashing;
    public bool FlashOn;
  }

  // Turns RPM into the shift light row state. Stateful only for the redline flash:
  // it starts in its off phase, so every visible change alters the number of
  // time-critical elements and bypasses the layer's refresh cap. A flash that ends
  // while lit (RPM drops below the redline, or the gear changes, in the flash's on
  // phase) goes dark for one half period rather than jumping straight to a
  // same-count band colour, so that transition stays urgent too.
  public sealed class ShiftLights {
    public const uint Green = 0xFF20D040u;
    public const uint Amber = 0xFFFFA000u;
    public const uint Red   = 0xFFFF2020u;
    public const uint Blue  = 0xFF2060FFu;
    public const double FlashHalfPeriod = 0.1;   // 5 Hz
    public const int MinLights = 3, MaxLights = 20;

    // Row range when the car has no usable shift points, as fractions of MaxRpm.
    const double FallbackStart = 0.75, FallbackRedline = 0.97;

    string _gear;
    bool _flashing;
    bool _heldAfterShift;   // gear changed at the redline; wait for RPM to leave it
    double _flashStart;
    double _darkUntil = double.NegativeInfinity;   // dark period after an on-phase flash end

    public static int LightCount(int requested) => Math.Max(MinLights, Math.Min(MaxLights, requested));

    // Left to right: green, then amber, then red. Red and amber keep at least one light.
    public static uint LightColor(int index, int count) {
      int red = Math.Max(1, (int)Math.Round(0.2 * count, MidpointRounding.AwayFromZero));
      int amber = Math.Max(1, (int)Math.Round(0.3 * count, MidpointRounding.AwayFromZero));
      if (index >= count - red) return Red;
      if (index >= count - red - amber) return Amber;
      return Green;
    }

    public ShiftState Update(ShiftInput input, double timeSeconds, int count) {
      bool gearChanged = _gear != null && input.Gear != _gear;
      _gear = input.Gear;

      if (!Range(input, out double start, out double redline) || !(input.Rpm > 0)) {
        _flashing = false;
        _heldAfterShift = false;
        _darkUntil = double.NegativeInfinity;
        return default;
      }

      if (input.Rpm < redline) {
        EndFlash(timeSeconds);
        _heldAfterShift = false;
      } else {
        if (gearChanged) { EndFlash(timeSeconds); _heldAfterShift = true; }
        if (!_flashing && !_heldAfterShift && timeSeconds >= _darkUntil) {
          _flashing = true; _flashStart = timeSeconds;
        }
      }

      if (_flashing) {
        return new ShiftState { Lit = count, Flashing = true, FlashOn = FlashOnAt(timeSeconds) };
      }

      if (timeSeconds < _darkUntil) return new ShiftState { Lit = 0 };

      double lit = Math.Ceiling(count * (input.Rpm - start) / (redline - start));
      return new ShiftState { Lit = (int)Math.Max(0, Math.Min(count, lit)) };
    }

    // Ends the current flash. If it was in its on (lit) phase at that moment, the row
    // must go dark for one half period rather than switch straight to a same-count
    // band colour, since that would be a colour-only change and not urgent.
    void EndFlash(double timeSeconds) {
      if (_flashing && FlashOnAt(timeSeconds)) _darkUntil = timeSeconds + FlashHalfPeriod;
      _flashing = false;
    }

    bool FlashOnAt(double timeSeconds) {
      long phase = (long)Math.Floor((timeSeconds - _flashStart) / FlashHalfPeriod);
      return phase % 2 == 1;
    }

    // Each end independently: the car's value when given, else 75% or 97% of MaxRpm.
    // If the two ends are unusable together, both fall back to MaxRpm. False when
    // nothing is usable.
    static bool Range(ShiftInput i, out double start, out double redline) {
      start = i.StartRpm > 0 ? i.StartRpm : FallbackStart * i.MaxRpm;
      redline = i.RedlineRpm > 0 ? i.RedlineRpm : FallbackRedline * i.MaxRpm;
      if (start > 0 && redline > start) return true;
      start = FallbackStart * i.MaxRpm; redline = FallbackRedline * i.MaxRpm;
      return i.MaxRpm > 0;
    }
  }
}
