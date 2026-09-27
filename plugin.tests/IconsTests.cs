using System;
using NUnit.Framework;
using OpenXRSimHubAlerts.Plugin;
using OpenXRSimHubAlerts.Shared;

public class IconsTests {
  [Test] public void EveryIconIdHasAnEmbeddedPng() {
    foreach (IconId id in Enum.GetValues(typeof(IconId)))
      using (var s = Icons.Open(id)) {
        Assert.That(s, Is.Not.Null, id.ToString());
        var sig = new byte[4]; s.Read(sig, 0, 4);
        Assert.That(sig[1], Is.EqualTo((byte)'P'), id.ToString());
      }
  }

  [Test] public void IconElementFillsItsBox() {
    var e = OverlayComposer.Icon(IconId.Tc, Eyes.Left, 0.2f, 0.3f, 0.05f, 0xFFFFE000u, 9);
    Assert.That(e.Kind, Is.EqualTo(ElementKind.Icon));
    Assert.That(e.Ref, Is.EqualTo((ushort)IconId.Tc));
    Assert.That((e.U, e.V, e.HalfW, e.HalfH), Is.EqualTo((0.2f, 0.3f, 0.05f, 0.05f)));
    Assert.That((e.Eyes, e.Color, e.Priority), Is.EqualTo((Eyes.Left, 0xFFFFE000u, (byte)9)));
  }
}
