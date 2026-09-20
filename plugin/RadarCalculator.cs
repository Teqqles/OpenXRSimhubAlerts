// plugin/RadarCalculator.cs
using System;
using System.Collections.Generic;
using OpenXRSimHubAlerts.Shared;

namespace OpenXRSimHubAlerts.Plugin {
  public struct Opponent {
    public bool HasRelative; public float RelX, RelY;
    public float SplineGap; public float TrackWidthEstimate;
  }

  public static class RadarCalculator {
    // side codes: 0 none,1 left,2 right,3 ahead,4 behind
    public static int Build(IReadOnlyList<Opponent> opps, float range, CarBlip[] outCars) {
      int n = 0;
      for (int i = 0; i < opps.Count && n < outCars.Length; i++) {
        var o = opps[i];
        float rx, ry;
        if (o.HasRelative) { rx = o.RelX; ry = o.RelY; }
        else {
          ry = o.SplineGap;
          // Lateral unknown from spline: assume just off-line, sign unknown -> treat as ahead/behind only.
          rx = 0f;
        }
        float dist = (float)Math.Sqrt(rx*rx + ry*ry);
        if (dist > range) continue;

        byte side;
        const float lateralThreshold = 1.2f; // meters: within this, it's ahead/behind not alongside
        if (o.HasRelative && Math.Abs(rx) > lateralThreshold)
          side = (byte)(rx > 0 ? 2 : 1);       // right : left
        else
          side = (byte)(ry >= 0 ? 3 : 4);      // ahead : behind

        outCars[n] = new CarBlip {
          Rel = new Vec2 { X = rx, Y = ry }, Distance = dist, Side = side, Flags = 0,
          Pad0 = 0, Pad1 = 0
        };
        n++;
      }
      MarkClosestPerSide(outCars, n);
      return n;
    }

    static void MarkClosestPerSide(CarBlip[] cars, int n) {
      // sides 1 (left), 2 (right), 4 (behind) get a closest-threat marker; ahead(3) never.
      foreach (byte s in new byte[] { 1, 2, 4 }) {
        int best = -1; float bd = float.MaxValue;
        for (int i = 0; i < n; i++)
          if (cars[i].Side == s && cars[i].Distance < bd) { bd = cars[i].Distance; best = i; }
        if (best >= 0) { var c = cars[best]; c.Flags |= 1; cars[best] = c; }
      }
    }
  }
}
