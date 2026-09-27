// plugin.tests/LayoutParityTests.cs
using System;
using System.Runtime.InteropServices;
using NUnit.Framework;
using OpenXRSimHubAlerts.Shared;

// Pins the C# mirror to the sizes and offsets in layer/tests/test_layout.cpp.
public class LayoutParityTests {
  [Test] public void ElementIs32Bytes() =>
    Assert.That(Marshal.SizeOf<Element>(), Is.EqualTo(32));

  [TestCase("U", 4)]
  [TestCase("Angle", 20)]
  [TestCase("Color", 24)]
  [TestCase("Ref", 28)]
  public void ElementOffsets(string field, int offset) =>
    Assert.That((int)Marshal.OffsetOf<Element>(field), Is.EqualTo(offset));

  [TestCase("RefreshMode", 9)]
  [TestCase("ElementCount", 12)]
  [TestCase("Elements", 16)]
  public void DataBlockOffsets(string field, int offset) =>
    Assert.That((int)Marshal.OffsetOf<DataBlock>(field), Is.EqualTo(offset));

  [Test] public void DataBlockSizeMatchesCppContract() =>
    Assert.That(Marshal.SizeOf<DataBlock>(), Is.EqualTo(16 + 32 * ShmContract.MaxElements));

  [Test] public void EnumsAreOneByte() {
    foreach (var t in new[] { typeof(RefreshMode), typeof(ElementKind), typeof(Eyes), typeof(ElementFlags) })
      Assert.That(Marshal.SizeOf(Enum.GetUnderlyingType(t)), Is.EqualTo(1), t.Name);
  }

  [Test] public void VersionKindAndFlagMatchCppContract() {
    Assert.That(ShmContract.Version, Is.EqualTo(5u));
    Assert.That((byte)ElementKind.Glow, Is.EqualTo(6));
    Assert.That((byte)ElementFlags.ForwardAnchored, Is.EqualTo(2));
  }
}
