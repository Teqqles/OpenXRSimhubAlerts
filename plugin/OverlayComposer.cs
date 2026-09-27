// plugin/OverlayComposer.cs
using System;
using OpenXRSimHubAlerts.Shared;

namespace OpenXRSimHubAlerts.Plugin {
  // Turns the active flags, radar cars and shift light state into the element list
  // the layer draws and the settings preview renders. The only place that decides
  // how alerts look.
  public static class OverlayComposer {
    // Radar is the collision warning, so it paints over flags. Lower alerts
    // (fuel, driver aids) belong below both.
    public const byte RadarPriority = 200;
    public const byte FlagPriority  = 150;
    public const byte ShiftPriority = 100;

    static readonly FlagType[] FlagOrder = {
      FlagType.Red, FlagType.Meatball, FlagType.Black, FlagType.Blue,
      FlagType.Yellow, FlagType.White, FlagType.Green,
    };

    // Non-bar flag shapes share the visual area of a circle of radius sz, so
    // swapping shape never changes apparent size.
    //   square side s:          s^2 = pi       -> half = sqrt(pi)/2
    //   triangle base=height:   2k^2 = pi      -> k    = sqrt(pi/2)
    //   rect (h = 0.6 w):       2.4 hw^2 = pi  -> hw   = sqrt(pi/2.4)
    const float SquareHalf = 0.8862269f;
    const float TriHalf    = 1.2533141f;
    const float RectHalfW  = 1.1441037f;
    const float RectHalfH  = 0.6864622f;

    const float FlagEdge    = 0.82f;       // bar inset, keeps it inside the lens
    const uint  MeatballBody = 0xFF101010u;
    const uint  MeatballDot  = 0xFFFF8000u;

    const float ShiftCore    = 0.025f;   // light radius at scale 1
    const float ShiftSpacing = 2.6f;     // centre to centre, in radii
    const float UnlitBright  = 0.2f;

    public static void Compose(Settings s, byte activeFlags, CarBlip[] cars, uint carCount,
                               ShiftState shift, ref DataBlock block) {
      if (block.Elements == null) block.Elements = new Element[ShmContract.MaxElements];
      var list = new ElementList(block.Elements);
      if (s.EnableFlags) AddFlag(s, (FlagType)activeFlags, list);
      // Radar before shift lights: if the list ever fills, the collision warning stays.
      if (s.EnableRadar) AddRadar(s, cars, carCount, list);
      if (s.EnableShiftLights) AddShiftLights(s, shift, list);
      block.RefreshMode = s.RefreshMode;
      block.ElementCount = (uint)list.Count;
    }

    static void AddFlag(Settings s, FlagType active, ElementList list) {
      FlagType flag = Array.Find(FlagOrder, f => (active & f) != 0);
      if (flag == FlagType.None) return;

      float alpha = s.FlagOpacity > 0 ? s.FlagOpacity : 1f;
      float scale = s.ScaleFlag > 0 ? s.ScaleFlag : 1f;
      bool meatball = flag == FlagType.Meatball;
      uint body = WithAlpha(meatball ? MeatballBody : FlagColor(flag), alpha);
      uint dot = WithAlpha(MeatballDot, alpha);

      if (s.Shape == 0) {
        // Bar: full-height stripe down the outer edge of each eye.
        float w = 0.05f * scale;
        list.Add(Shape(ElementKind.Rect, Eyes.Left, -FlagEdge, 0, w, 1, 0, body, FlagPriority));
        list.Add(Shape(ElementKind.Rect, Eyes.Right, FlagEdge, 0, w, 1, 0, body, FlagPriority));
        if (meatball) {
          list.Add(Shape(ElementKind.Ellipse, Eyes.Left, -FlagEdge, 0, 2 * w, 2 * w, 0, dot, FlagPriority));
          list.Add(Shape(ElementKind.Ellipse, Eyes.Right, FlagEdge, 0, 2 * w, 2 * w, 0, dot, FlagPriority));
        }
        return;
      }

      // Other shapes: a marker mirrored to the outer edge of each eye.
      float sz = 0.15f * scale;
      float x = Math.Abs(s.PosFlagx), y = s.PosFlagy;
      list.Add(FlagShape(s.Shape, Eyes.Left, -x, y, sz, body));
      list.Add(FlagShape(s.Shape, Eyes.Right, x, y, sz, body));
      if (meatball) {
        // Centre the disc on the shape's centroid; the apex-up triangle's sits a
        // third of its half-height below centre.
        float dy = y - (s.Shape == 4 ? TriHalf * sz / 3 : 0);
        float r = 0.45f * sz;
        list.Add(Shape(ElementKind.Ellipse, Eyes.Left, -x, dy, r, r, 0, dot, FlagPriority));
        list.Add(Shape(ElementKind.Ellipse, Eyes.Right, x, dy, r, r, 0, dot, FlagPriority));
      }
    }

    static Element FlagShape(byte shape, Eyes eyes, float u, float v, float sz, uint color) {
      switch (shape) {
        case 1:  return Shape(ElementKind.Rect, eyes, u, v, RectHalfW * sz, RectHalfH * sz, 0, color, FlagPriority);
        case 3:  return Shape(ElementKind.Ellipse, eyes, u, v, sz, sz, 0, color, FlagPriority);
        case 4:  return Shape(ElementKind.Triangle, eyes, u, v, TriHalf * sz, TriHalf * sz, 0, color, FlagPriority);
        default: return Shape(ElementKind.Rect, eyes, u, v, SquareHalf * sz, SquareHalf * sz, 0, color, FlagPriority);
      }
    }

    // One blip per car on a ring centred on each lens: left cars in the left eye,
    // right cars in the right eye, cars behind in both. Cars ahead are not drawn.
    // Colour and opacity encode distance: close is bright, opaque red.
    static void AddRadar(Settings s, CarBlip[] cars, uint carCount, ElementList list) {
      float range = s.RadarRange > 0 ? s.RadarRange : 1f;
      float ceiling = s.RadarMaxOpacity > 0 ? s.RadarMaxOpacity : 1f;
      float scale = s.ScaleRadar > 0 ? s.ScaleRadar : 1f;
      float halfW = 0.03f * scale;
      float radius = Math.Min(0.8f * scale, 0.85f);   // keeps side and rear blips on-lens

      for (int i = 0; i < carCount && i < cars.Length; i++) {
        CarBlip c = cars[i];
        Eyes eyes = c.Side == 1 ? Eyes.Left : c.Side == 2 ? Eyes.Right : c.Side == 4 ? Eyes.Both : Eyes.None;
        if (eyes == Eyes.None) continue;

        float closeness = 1f - Math.Min(c.Distance / range, 1f);
        float bearing = (float)Math.Atan2(c.Rel.X, -c.Rel.Y);   // 0 behind, +/- sides
        uint color = Shade(0xFFFF0000u, 0.35f + 0.65f * closeness, ceiling * (0.15f + 0.85f * closeness));
        float u = radius * (float)Math.Sin(bearing);
        float v = -radius * (float)Math.Cos(bearing);

        Element blip = s.RadarShape == 1
          ? Shape(ElementKind.Triangle, eyes, u, v, 1.2f * halfW, 1.7f * halfW, (float)Math.PI - bearing, color, RadarPriority)
          : Shape(ElementKind.Rect, eyes, u, v, halfW, 1.8f * halfW, 0, color, RadarPriority);
        blip.Flags = ElementFlags.TimeCritical;
        list.Add(blip);
      }
    }

    // A row of round lights filling left to right, split across the eyes: the left
    // half only in the left eye, the right half only in the right eye, the middle
    // light of an odd row in both. Forward anchoring puts u = 0 straight ahead in
    // each eye, so the halves meet at the true centre and no light is doubled.
    // Lit lights are a glow halo under a core, both time-critical; unlit lights are
    // dim cores or nothing.
    static void AddShiftLights(Settings s, ShiftState shift, ElementList list) {
      int count = ShiftLights.LightCount(s.ShiftLightCount);
      float scale = s.ScaleShift > 0 ? s.ScaleShift : 1f;
      float alpha = s.ShiftOpacity > 0 ? s.ShiftOpacity : 1f;
      float glow = Math.Max(0f, Math.Min(1f, s.ShiftGlow));
      float r = ShiftCore * scale;
      float step = ShiftSpacing * r;
      float left = s.PosShiftx - step * (count - 1) / 2f;

      for (int i = 0; i < count; i++) {
        float u = left + step * i;
        Eyes eyes = 2 * i + 1 < count ? Eyes.Left : 2 * i + 1 > count ? Eyes.Right : Eyes.Both;
        bool lit = shift.Flashing ? shift.FlashOn : i < shift.Lit;
        uint bandColor = ShiftLights.LightColor(i, count);

        if (!lit) {
          if (s.ShowUnlitLights) {
            // Dim cores always shade their own band colour, even mid-flash: only lit
            // cores and glows turn blue, so a flash toggling off does not also change
            // the unlit cores' colour.
            Element dim = Shape(ElementKind.Ellipse, eyes, u, s.PosShifty, r, r, 0,
                                Shade(bandColor, UnlitBright, alpha), ShiftPriority);
            dim.Flags = ElementFlags.ForwardAnchored;
            list.Add(dim);
          }
          continue;
        }

        uint litColor = shift.Flashing ? ShiftLights.Blue : bandColor;
        if (glow > 0) {
          float halo = r * (1 + 2 * glow);
          Element g = Shape(ElementKind.Glow, eyes, u, s.PosShifty, halo, halo, 0,
                            WithAlpha(litColor, alpha * glow), ShiftPriority);
          g.Flags = ElementFlags.TimeCritical | ElementFlags.ForwardAnchored;
          list.Add(g);
        }
        Element core = Shape(ElementKind.Ellipse, eyes, u, s.PosShifty, r, r, 0,
                             WithAlpha(litColor, alpha), ShiftPriority);
        core.Flags = ElementFlags.TimeCritical | ElementFlags.ForwardAnchored;
        list.Add(core);
      }
    }

    static Element Shape(ElementKind kind, Eyes eyes, float u, float v, float hw, float hh,
                         float angle, uint color, byte priority) =>
      new Element {
        Kind = kind, Eyes = eyes, Priority = priority,
        U = u, V = v, HalfW = hw, HalfH = hh, Angle = angle, Color = color,
      };

    static uint FlagColor(FlagType flag) {
      switch (flag) {
        case FlagType.Red:    return 0xFFFF2020u;
        case FlagType.Black:  return 0xFF101010u;
        case FlagType.Blue:   return 0xFF2060FFu;
        case FlagType.Yellow: return 0xFFFFE000u;
        case FlagType.White:  return 0xFFF0F0F0u;
        case FlagType.Green:  return 0xFF20D040u;
        default:              return 0u;
      }
    }

    static uint WithAlpha(uint argb, float alpha) => Shade(argb, 1f, alpha);

    // Scales RGB by `bright` and replaces alpha, both clamped.
    static uint Shade(uint argb, float bright, float alpha) {
      uint r = ToByte(((argb >> 16) & 0xFF) / 255f * bright);
      uint g = ToByte(((argb >> 8) & 0xFF) / 255f * bright);
      uint b = ToByte((argb & 0xFF) / 255f * bright);
      return ToByte(alpha) << 24 | r << 16 | g << 8 | b;
    }

    static uint ToByte(float unit) => (uint)Math.Round(Math.Max(0f, Math.Min(1f, unit)) * 255f);

    // Fills a fixed element array, dropping anything past its capacity.
    sealed class ElementList {
      readonly Element[] _items;
      public int Count { get; private set; }
      public ElementList(Element[] items) { _items = items; }
      public void Add(Element e) { if (Count < _items.Length) _items[Count++] = e; }
    }
  }
}
