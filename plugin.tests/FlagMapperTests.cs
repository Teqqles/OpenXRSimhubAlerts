using NUnit.Framework;
using OpenXRSimHubAlerts.Shared;
using OpenXRSimHubAlerts.Plugin;

namespace OpenXRSimHubAlerts.Plugin.Tests {
  public class FlagMapperTests {
    [Test] public void MapsSingleFlag() {
      var m = FlagMapper.Map(new FlagInput { Blue = true });
      Assert.That(m, Is.EqualTo((byte)FlagType.Blue));
    }
    [Test] public void MeatballIndependentOfBlack() {
      var m = FlagMapper.Map(new FlagInput { Meatball = true });
      Assert.That((m & (byte)FlagType.Meatball), Is.Not.Zero);
      Assert.That((m & (byte)FlagType.Black), Is.Zero);
    }
    [Test] public void MultipleFlagsCombine() {
      var m = FlagMapper.Map(new FlagInput { Yellow = true, Blue = true });
      Assert.That(m, Is.EqualTo((byte)(FlagType.Yellow | FlagType.Blue)));
    }
  }
}
