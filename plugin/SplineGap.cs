// plugin/SplineGap.cs
namespace OpenXRSimHubAlerts.Plugin {
  public static class SplineGap {
    /// <summary>
    /// Computes signed spline gap in meters: + ahead, - behind.
    /// Handles wraparound at track start/finish (e.g., player 0.99 / opponent 0.01 → small positive).
    /// </summary>
    /// <param name="opponentPos">Opponent track position (0.0-1.0 normalized)</param>
    /// <param name="playerPos">Player track position (0.0-1.0 normalized)</param>
    /// <param name="trackLength">Track length in meters</param>
    /// <returns>Signed gap in meters (+ ahead, - behind)</returns>
    public static float Compute(double opponentPos, double playerPos, double trackLength) {
      double gap = (opponentPos - playerPos) * trackLength;

      // Handle wrap-around at track start/finish
      double halfTrack = trackLength / 2;
      if (gap > halfTrack)
        gap -= trackLength;
      else if (gap < -halfTrack)
        gap += trackLength;

      return (float)gap;
    }
  }
}
