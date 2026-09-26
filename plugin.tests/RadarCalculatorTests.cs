// plugin.tests/RadarCalculatorTests.cs
using System.Collections.Generic;
using NUnit.Framework;
using OpenXRSimHubAlerts.Plugin;

public class RadarCalculatorTests {
  static CarBlip[] Buf() => new CarBlip[RadarCalculator.MaxCars];

  [Test] public void RelativeCarOnRightClassifiedRight() {
    var buf = Buf();
    var n = RadarCalculator.Build(
      new[] { new Opponent { HasRelative = true, RelX = 3f, RelY = 1f } }, 80f, buf);
    Assert.That(n, Is.EqualTo(1));
    Assert.That(buf[0].Side, Is.EqualTo((byte)2)); // right
    Assert.That(buf[0].Distance, Is.EqualTo((float)System.Math.Sqrt(10)).Within(1e-3));
  }

  [Test] public void OutOfRangeDropped() {
    var buf = Buf();
    var n = RadarCalculator.Build(
      new[] { new Opponent { HasRelative = true, RelX = 200f, RelY = 0f } }, 80f, buf);
    Assert.That(n, Is.EqualTo(0));
  }

  [Test] public void SplineFallbackBehind() {
    var buf = Buf();
    var n = RadarCalculator.Build(
      new[] { new Opponent { HasRelative = false, SplineGap = -20f, TrackWidthEstimate = 10f } },
      80f, buf);
    Assert.That(n, Is.EqualTo(1));
    Assert.That(buf[0].Side, Is.EqualTo((byte)4)); // behind
  }

  [Test] public void ClosestThreatBitSetPerSide() {
    var buf = Buf();
    RadarCalculator.Build(new[] {
      new Opponent { HasRelative = true, RelX = 3f, RelY = 0f },   // right, near
      new Opponent { HasRelative = true, RelX = 3f, RelY = 40f },  // right, far
    }, 80f, buf);
    Assert.That(buf[0].Flags & 1, Is.EqualTo(1)); // nearer right car flagged
    Assert.That(buf[1].Flags & 1, Is.EqualTo(0));
  }
}
