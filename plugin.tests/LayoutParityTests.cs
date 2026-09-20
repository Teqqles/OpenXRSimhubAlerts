// plugin.tests/LayoutParityTests.cs
using System.Runtime.InteropServices;
using NUnit.Framework;
using OpenXRSimHubAlerts.Shared;

public class LayoutParityTests {
  [Test] public void DataBlockSizeMatchesCppContract() {
    Assert.That(Marshal.SizeOf<DataBlock>(), Is.EqualTo(1116));
  }
  [Test] public void CarBlipSizeIs16() =>
    Assert.That(Marshal.SizeOf<CarBlip>(), Is.EqualTo(16));
  [Test] public void ConfigSizeIs76() =>
    Assert.That(Marshal.SizeOf<Config>(), Is.EqualTo(76));
}
