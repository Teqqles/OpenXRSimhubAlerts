// plugin.tests/DriverAidsTests.cs
using NUnit.Framework;
using OpenXRSimHubAlerts.Plugin;

public class DriverAidsTests {
  static DriverAidInput In(bool abs = false, bool tc = false, bool avail = false, bool open = false) =>
    new DriverAidInput { Abs = abs, Tc = tc, DrsAvailable = avail, DrsOpen = open };

  [Test] public void NothingActiveShowsNothing() {
    var s = new DriverAids().Update(In(), 0);
    Assert.That((s.Abs, s.Tc, s.Drs), Is.EqualTo((false, false, DrsState.Off)));
  }

  [Test] public void AbsHoldsFor200msAfterTheLastActiveSample() {
    var d = new DriverAids();
    d.Update(In(abs: true), 1.00);
    Assert.That(d.Update(In(), 1.15).Abs, Is.True);
    Assert.That(d.Update(In(), 1.25).Abs, Is.False);
  }

  [Test] public void TcHoldsIndependentlyOfAbs() {
    var d = new DriverAids();
    d.Update(In(tc: true), 2.0);
    var s = d.Update(In(abs: true), 2.1);
    Assert.That((s.Abs, s.Tc), Is.EqualTo((true, true)));
    Assert.That(d.Update(In(), 2.25).Tc, Is.False);
  }

  [Test] public void ARetriggerExtendsTheHold() {
    var d = new DriverAids();
    d.Update(In(abs: true), 0.0);
    d.Update(In(abs: true), 0.15);
    Assert.That(d.Update(In(), 0.3).Abs, Is.True);
  }

  [TestCase(false, false, DrsState.Off)]
  [TestCase(true, false, DrsState.Available)]
  [TestCase(true, true, DrsState.Open)]
  [TestCase(false, true, DrsState.Open)]
  public void DrsState_(bool avail, bool open, DrsState expected) =>
    Assert.That(new DriverAids().Update(In(avail: avail, open: open), 0).Drs, Is.EqualTo(expected));
}
