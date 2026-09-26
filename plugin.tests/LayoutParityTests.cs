// plugin.tests/LayoutParityTests.cs
using System.Runtime.InteropServices;
using NUnit.Framework;
using OpenXRSimHubAlerts.Shared;

public class LayoutParityTests {
  [Test] public void DataBlockSizeMatchesCppContract() {
    Assert.That(Marshal.SizeOf<DataBlock>(), Is.EqualTo(1132));
  }
  [Test] public void CarBlipSizeIs16() =>
    Assert.That(Marshal.SizeOf<CarBlip>(), Is.EqualTo(16));
  [Test] public void ConfigSizeIs92() =>
    Assert.That(Marshal.SizeOf<Config>(), Is.EqualTo(92));
  [Test] public void RefreshModeOffsetMatchesCppContract() =>
    Assert.That((int)Marshal.OffsetOf<Config>("RefreshMode"), Is.EqualTo(5));
  [Test] public void RefreshModeIsOneByte() =>
    Assert.That(Marshal.SizeOf(System.Enum.GetUnderlyingType(typeof(RefreshMode))), Is.EqualTo(1));
}
