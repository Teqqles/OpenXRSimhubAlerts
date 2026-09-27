// plugin.tests/HeadsetMasksTests.cs
using System.Linq;
using NUnit.Framework;
using OpenXRSimHubAlerts.Plugin;

public class HeadsetMasksTests {
  [Test] public void UnknownHeadsetShowsTheWholeImage() {
    var b = HeadsetMasks.Bounds("Other", leftEye: true);
    Assert.That((b.Left, b.Right, b.Bottom, b.Top), Is.EqualTo((-1.0, 1.0, -1.0, 1.0)));
  }

  [Test] public void LeftEyeOuterEdgeIsItsLeftEdge() {
    var area = HeadsetMasks.Presets.Single(p => p.Name == "Meta Quest 3").Area;
    var b = HeadsetMasks.Bounds("Meta Quest 3", leftEye: true);
    Assert.That(b.Left, Is.EqualTo(-area.Outer));
    Assert.That(b.Right, Is.EqualTo(area.Inner));
    Assert.That(b.Top, Is.EqualTo(area.Top));
    Assert.That(b.Bottom, Is.EqualTo(-area.Bottom));
  }

  [Test] public void RightEyeMirrorsTheLeft() {
    var left = HeadsetMasks.Bounds("Meta Quest 3", leftEye: true);
    var right = HeadsetMasks.Bounds("Meta Quest 3", leftEye: false);
    Assert.That(right.Left, Is.EqualTo(-left.Right));
    Assert.That(right.Right, Is.EqualTo(-left.Left));
    Assert.That((right.Bottom, right.Top), Is.EqualTo((left.Bottom, left.Top)));
  }

  [Test] public void EveryPresetIsInsideTheImage() {
    foreach (var (name, a) in HeadsetMasks.Presets)
      foreach (var edge in new[] { a.Top, a.Bottom, a.Outer, a.Inner })
        Assert.That(edge, Is.GreaterThan(0.5).And.LessThanOrEqualTo(1.0), name);
  }

  [Test] public void PresetNamesAreUnique() =>
    Assert.That(HeadsetMasks.Presets.Select(p => p.Name), Is.Unique);
}
