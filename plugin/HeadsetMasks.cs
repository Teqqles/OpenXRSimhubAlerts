// plugin/HeadsetMasks.cs
namespace OpenXRSimHubAlerts.Plugin {
  // Preview-only guide to how much of each eye's rendered image you can see past
  // the lens and facial interface. The preview crops each eye to it. Not sent to
  // the layer.
  //
  // Estimates: no per-edge visible-area data is published, and the facial
  // interface depends on the wearer. Each preset starts from an overall visible
  // fraction per axis and assumes the bottom (cheeks) and nose side lose more than
  // the top and outer side: a 60/40 split of each axis's margin.
  public static class HeadsetMasks {
    // Visible extent of each edge as NDC distance from the image centre (1 = edge).
    public struct VisibleArea {
      public double Top, Bottom, Outer, Inner;
      public VisibleArea(double top, double bottom, double outer, double inner) {
        Top = top; Bottom = bottom; Outer = outer; Inner = inner;
      }
    }

    public static readonly (string Name, VisibleArea Area)[] Presets = {
      ("Meta Quest 3",        new VisibleArea(0.90, 0.86, 0.94, 0.90)),
      ("Meta Quest 2",        new VisibleArea(0.86, 0.78, 0.89, 0.83)),
      ("Meta Quest Pro",      new VisibleArea(0.92, 0.88, 0.94, 0.92)),
      ("Valve Index",         new VisibleArea(0.87, 0.81, 0.91, 0.87)),
      ("PSVR2",               new VisibleArea(0.90, 0.84, 0.93, 0.89)),
      ("Pico 4",              new VisibleArea(0.91, 0.87, 0.95, 0.93)),
      ("Pico 4 Ultra",        new VisibleArea(0.91, 0.87, 0.95, 0.93)),
      ("HTC Vive XR Elite",   new VisibleArea(0.88, 0.82, 0.92, 0.88)),
      ("Bigscreen Beyond",    new VisibleArea(0.94, 0.90, 0.96, 0.94)),
      ("Apple Vision Pro",    new VisibleArea(0.95, 0.93, 0.97, 0.95)),
      ("Pimax Crystal",       new VisibleArea(0.90, 0.86, 0.94, 0.92)),
      ("Pimax Crystal Light", new VisibleArea(0.90, 0.86, 0.94, 0.92)),
      ("Pimax Crystal Super", new VisibleArea(0.90, 0.86, 0.94, 0.92)),
      ("Pimax 8KX",           new VisibleArea(0.86, 0.78, 0.90, 0.84)),
      ("Pimax 8K+",           new VisibleArea(0.86, 0.78, 0.90, 0.84)),
      ("Pimax 5K Super",      new VisibleArea(0.86, 0.78, 0.90, 0.84)),
      ("Pimax Artisan",       new VisibleArea(0.86, 0.78, 0.90, 0.84)),
      ("Pimax Vision 12K",    new VisibleArea(0.91, 0.87, 0.95, 0.93)),
      ("Pimax Dream Air",     new VisibleArea(0.88, 0.82, 0.92, 0.88)),
      ("Pimax Dream Air SE",  new VisibleArea(0.88, 0.82, 0.92, 0.88)),
      ("Steam Frame",         new VisibleArea(0.90, 0.84, 0.94, 0.90)),
    };

    // Visible NDC bounds for one eye (y up). The outer edge is the left eye's left
    // and the right eye's right. Unknown headsets show the whole image.
    public static (double Left, double Right, double Bottom, double Top) Bounds(string headset, bool leftEye) {
      foreach (var (name, a) in Presets)
        if (name == headset)
          return leftEye ? (-a.Outer, a.Inner, -a.Bottom, a.Top)
                         : (-a.Inner, a.Outer, -a.Bottom, a.Top);
      return (-1, 1, -1, 1);
    }

    // The visible area as a pixel rectangle (y down) on a size x size eye canvas,
    // for cropping the preview to what the headset shows.
    public static (double X, double Y, double Width, double Height) VisiblePixels(string headset, bool leftEye, double size) {
      var (left, right, bottom, top) = Bounds(headset, leftEye);
      return ((left + 1) / 2 * size, (1 - top) / 2 * size,
              (right - left) / 2 * size, (top - bottom) / 2 * size);
    }
  }
}
