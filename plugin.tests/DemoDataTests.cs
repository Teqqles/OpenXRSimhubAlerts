// plugin.tests/DemoDataTests.cs
using NUnit.Framework;
using OpenXRSimHubAlerts.Plugin;

public class DemoDataTests {
  static CarBlip[] Buf() => new CarBlip[RadarCalculator.MaxCars];

  [Test] public void FillsThreeCarsNeverAhead() {
    var buf = Buf();
    for (double t = 0; t < 12; t += 0.25) {
      var n = DemoData.Fill(t, buf, out _);
      Assert.That(n, Is.EqualTo(3));
      for (int i = 0; i < n; i++) {
        // Side must be left(1), right(2) or behind(4); never ahead(3) or none(0).
        Assert.That(buf[i].Side, Is.AnyOf((byte)1, (byte)2, (byte)4),
          $"car {i} at t={t} had side {buf[i].Side}");
      }
    }
  }

  [Test] public void ClosestCarFlaggedAsThreat() {
    var buf = Buf();
    DemoData.Fill(0.0, buf, out _);
    Assert.That(buf[0].Flags & 1, Is.EqualTo(1)); // closest (left) car is the threat
  }

  [Test] public void FlagCycleCoversEveryFlag() {
    var buf = Buf();
    var seen = new System.Collections.Generic.HashSet<byte>();
    // Sample across one full cycle (7 flags * 1.5s each).
    for (double t = 0; t < DemoData.FlagPeriodSeconds * 7; t += 0.1) {
      DemoData.Fill(t, buf, out byte flags);
      seen.Add(flags);
      Assert.That(flags, Is.Not.EqualTo((byte)0)); // demo always shows a flag
    }
    // Green, Yellow, Blue, White, Red, Black, Meatball
    Assert.That(seen, Is.SupersetOf(new byte[] { 1, 2, 4, 8, 16, 32, 64 }));
  }

  [Test] public void ShiftSweepReachesTheRedlineEachGear() {
    bool reached = false, belowStart = false;
    for (double t = 0; t < DemoData.ShiftPeriodSeconds; t += 0.05) {
      var s = DemoData.Shift(t);
      reached |= s.Rpm >= s.RedlineRpm;
      belowStart |= s.Rpm < s.StartRpm;
    }
    Assert.That(reached, "holds the redline so the flash shows");
    Assert.That(belowStart, "starts below the first light");
  }

  [Test] public void ShiftSweepChangesGearEachPeriod() {
    var a = DemoData.Shift(0.5);
    var b = DemoData.Shift(0.5 + DemoData.ShiftPeriodSeconds);
    Assert.That(a.Gear, Is.Not.EqualTo(b.Gear));
    Assert.That(a.Rpm, Is.EqualTo(b.Rpm), "same sweep in every gear");
  }
}
