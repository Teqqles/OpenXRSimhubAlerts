// plugin.tests/SharedMemoryWriterTests.cs
using NUnit.Framework;
using OpenXRSimHubAlerts.Shared;
using OpenXRSimHubAlerts.Plugin;

namespace OpenXRSimHubAlerts.Plugin.Tests {
  public class SharedMemoryWriterTests {
    [Test] public void WriteThenReadRoundTrips() {
      string name = "OpenXRSimHubAlertsTest-" + System.Guid.NewGuid();
      using var w = new SharedMemoryWriter(name);
      var b = new DataBlock {
        Connected = 1, RefreshMode = RefreshMode.Fps30,
        ElementCount = 1, Elements = new Element[ShmContract.MaxElements],
      };
      b.Elements[0] = new Element { Kind = ElementKind.Ellipse, Eyes = Eyes.Both, U = 0.25f, Color = 0xFF2060FFu };
      w.Write(ref b);

      Assert.That(SharedMemoryWriter.TryReadRaw(out var r, name), Is.True);
      Assert.That(r.Version, Is.EqualTo(ShmContract.Version));
      Assert.That(r.Seq % 2, Is.EqualTo(0u));           // even after write
      Assert.That(r.RefreshMode, Is.EqualTo(RefreshMode.Fps30));
      Assert.That(r.ElementCount, Is.EqualTo(1u));
      Assert.That(r.Elements[0].Kind, Is.EqualTo(ElementKind.Ellipse));
      Assert.That(r.Elements[0].U, Is.EqualTo(0.25f));
      Assert.That(r.Elements[0].Color, Is.EqualTo(0xFF2060FFu));
    }
  }
}
