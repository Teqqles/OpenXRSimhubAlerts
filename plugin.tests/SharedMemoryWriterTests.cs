// plugin.tests/SharedMemoryWriterTests.cs
using NUnit.Framework;
using OpenXRSimHubAlerts.Shared;
using OpenXRSimHubAlerts.Plugin;

public class SharedMemoryWriterTests {
  [Test] public void WriteThenReadRoundTrips() {
    using var w = new SharedMemoryWriter();
    var b = new DataBlock {
      Connected = 1, ActiveFlags = (byte)FlagType.Yellow,
      CarCount = 1, Cars = new CarBlip[ShmContract.MaxCars],
      Config = new Config { ColorOverride = new uint[8], RadarRange = 80f }
    };
    b.Cars[0] = new CarBlip { Distance = 12.5f, Side = 1 };
    w.Write(ref b);

    Assert.That(SharedMemoryWriter.TryReadRaw(out var r), Is.True);
    Assert.That(r.Version, Is.EqualTo(ShmContract.Version));
    Assert.That(r.Seq % 2, Is.EqualTo(0u));           // even after write
    Assert.That(r.ActiveFlags, Is.EqualTo((byte)FlagType.Yellow));
    Assert.That(r.Cars[0].Distance, Is.EqualTo(12.5f));
  }
}
