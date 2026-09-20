using NUnit.Framework;
using OpenXRSimHubAlerts.Plugin;

namespace OpenXRSimHubAlerts.Plugin.Tests {
  public class SplineGapTests {
    [Test]
    public void NormalAhead() {
      // Player at 10%, opponent at 20% → opponent is 100m ahead
      float gap = SplineGap.Compute(opponentPos: 0.20, playerPos: 0.10, trackLength: 1000);
      Assert.AreEqual(100f, gap, 0.01f);
    }

    [Test]
    public void NormalBehind() {
      // Player at 20%, opponent at 10% → opponent is 100m behind
      float gap = SplineGap.Compute(opponentPos: 0.10, playerPos: 0.20, trackLength: 1000);
      Assert.AreEqual(-100f, gap, 0.01f);
    }

    [Test]
    public void WrapAheadAcrossStartFinish() {
      // Player at 99%, opponent at 1% → opponent is ~20m ahead (not -980m behind)
      float gap = SplineGap.Compute(opponentPos: 0.01, playerPos: 0.99, trackLength: 1000);
      Assert.AreEqual(20f, gap, 0.01f);
    }

    [Test]
    public void WrapBehindAcrossStartFinish() {
      // Player at 1%, opponent at 99% → opponent is ~20m behind (not +980m ahead)
      float gap = SplineGap.Compute(opponentPos: 0.99, playerPos: 0.01, trackLength: 1000);
      Assert.AreEqual(-20f, gap, 0.01f);
    }
  }
}
