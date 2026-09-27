// plugin.tests/ShiftLightsTests.cs
using System.Linq;
using NUnit.Framework;
using OpenXRSimHubAlerts.Plugin;

public class ShiftLightsTests {
  const int N = 10;

  static ShiftInput In(double rpm, string gear = "3", double start = 5000, double redline = 7800, double max = 8000) =>
    new ShiftInput { Rpm = rpm, MaxRpm = max, StartRpm = start, RedlineRpm = redline, Gear = gear };

  static ShiftState Once(ShiftInput i, double t = 0) => new ShiftLights().Update(i, t, N);

  [TestCase(4000, 0)]
  [TestCase(5000, 0)]
  [TestCase(5001, 1)]
  [TestCase(6400, 5)]
  [TestCase(7799, 10)]
  public void FillsAcrossTheRange(double rpm, int lit) =>
    Assert.That(Once(In(rpm)).Lit, Is.EqualTo(lit));

  [TestCase(double.NaN)]
  [TestCase(0)]
  [TestCase(-100)]
  public void InvalidRpmLightsNothing(double rpm) {
    var s = Once(In(rpm));
    Assert.That(s.Lit, Is.EqualTo(0));
    Assert.That(s.Flashing, Is.False);
  }

  [Test] public void MissingShiftPointsFallBackToMaxRpm() =>
    // 75% to 97% of 8000 is 6000 to 7760; 6880 is half way.
    Assert.That(Once(In(6880, start: 0, redline: 0)).Lit, Is.EqualTo(5));

  [Test] public void InvertedShiftPointsFallBackToMaxRpm() =>
    Assert.That(Once(In(6880, start: 8000, redline: 7000)).Lit, Is.EqualTo(5));

  [Test] public void NoShiftPointsAndNoMaxRpmLightsNothing() =>
    Assert.That(Once(In(6000, start: 0, redline: 0, max: 0)).Lit, Is.EqualTo(0));

  [TestCase(3, 1, 1, 1)]
  [TestCase(10, 5, 3, 2)]
  [TestCase(20, 10, 6, 4)]
  public void ColourBandsGreenAmberRed(int count, int green, int amber, int red) {
    var colours = Enumerable.Range(0, count).Select(i => ShiftLights.LightColor(i, count)).ToArray();
    Assert.That(colours.Take(green), Is.All.EqualTo(ShiftLights.Green));
    Assert.That(colours.Skip(green).Take(amber), Is.All.EqualTo(ShiftLights.Amber));
    Assert.That(colours.Skip(green + amber).ToArray(), Has.Length.EqualTo(red).And.All.EqualTo(ShiftLights.Red));
  }

  [TestCase(1, 3)]
  [TestCase(10, 10)]
  [TestCase(50, 20)]
  public void LightCountIsClamped(int requested, int expected) =>
    Assert.That(ShiftLights.LightCount(requested), Is.EqualTo(expected));

  [Test] public void RedlineFlashStartsOffThenToggles() {
    var sl = new ShiftLights();
    var a = sl.Update(In(7800), 1.00, N);
    var b = sl.Update(In(7800), 1.15, N);
    var c = sl.Update(In(7800), 1.25, N);
    Assert.That(a.Flashing && b.Flashing && c.Flashing);
    Assert.That(new[] { a.FlashOn, b.FlashOn, c.FlashOn }, Is.EqualTo(new[] { false, true, false }));
    Assert.That(a.Lit, Is.EqualTo(N));
  }

  [Test] public void FlashEndsWhenRpmDrops() {
    var sl = new ShiftLights();
    sl.Update(In(7800), 0, N);
    var s = sl.Update(In(7000), 0.05, N);
    Assert.That(s.Flashing, Is.False);
    Assert.That(s.Lit, Is.EqualTo(8));   // ceil(10 * 2000 / 2800)
  }

  [Test] public void FlashEndsOnGearChangeAndWaitsForRpmToLeaveTheRedline() {
    var sl = new ShiftLights();
    sl.Update(In(7800, "3"), 0, N);
    var changed = sl.Update(In(7800, "4"), 0.05, N);
    var stillHigh = sl.Update(In(7900, "4"), 0.5, N);
    Assert.That(changed.Flashing, Is.False);
    Assert.That(changed.Lit, Is.EqualTo(N));
    Assert.That(stillHigh.Flashing, Is.False);

    sl.Update(In(7000, "4"), 0.6, N);
    var again = sl.Update(In(7800, "4"), 0.7, N);
    Assert.That(again.Flashing, Is.True);
    Assert.That(again.FlashOn, Is.False);
  }
}
