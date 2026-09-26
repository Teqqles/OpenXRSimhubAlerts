// plugin/ui/SettingsControl.xaml.cs
using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Shapes;
using System.Windows.Threading;
using OpenXRSimHubAlerts.Shared;

namespace OpenXRSimHubAlerts.Plugin.ui {
  public partial class SettingsControl : UserControl {
    readonly Settings _s;

    // Animated preview: driven by the same DemoData generator the plugin uses in
    // Demo mode, so both eyes show the actual cycling flags + orbiting radar the
    // layer would render. The 2D placement/shape/opacity math below mirrors
    // overlay.cpp (FlagColor, flag u/v/size/shape, radar ring, per-eye stereo,
    // closeness-scaled opacity) -- keep the two in sync.
    readonly DispatcherTimer _previewTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(50) };
    readonly System.Diagnostics.Stopwatch _previewClock = new System.Diagnostics.Stopwatch();
    readonly CarBlip[] _previewCars = new CarBlip[ShmContract.MaxCars];
    static readonly byte[] FlagPriority = { 16, 64, 32, 4, 2, 8, 1 }; // Red,Meatball,Black,Blue,Yellow,White,Green

    // Approximate visible-area fractions (horizontal, vertical) per headset.
    // Preview-only guide -- NOT sent to the layer. "Other"/unknown draws no mask.
    static readonly (string Name, double H, double V)[] Headsets = {
      ("Meta Quest 3",        0.92, 0.88),
      ("Meta Quest 2",        0.86, 0.82),
      ("Meta Quest Pro",      0.93, 0.90),
      ("Valve Index",         0.89, 0.84),
      ("PSVR2",               0.91, 0.87),
      ("Pico 4",              0.94, 0.89),
      ("Pico 4 Ultra",        0.94, 0.89),
      ("HTC Vive XR Elite",   0.90, 0.85),
      ("Bigscreen Beyond",    0.95, 0.92),
      ("Apple Vision Pro",    0.96, 0.94),
      ("Pimax Crystal",       0.93, 0.88),
      ("Pimax Crystal Light", 0.93, 0.88),
      ("Pimax Crystal Super", 0.93, 0.88),
      ("Pimax 8KX",           0.87, 0.82),
      ("Pimax 8K+",           0.87, 0.82),
      ("Pimax 5K Super",      0.87, 0.82),
      ("Pimax Artisan",       0.87, 0.82),
      ("Pimax Vision 12K",    0.94, 0.89),
      ("Pimax Dream Air",     0.90, 0.85),  // estimate: no published visible-area data
      ("Pimax Dream Air SE",  0.90, 0.85),  // estimate: no published visible-area data
      ("Steam Frame",         0.92, 0.87),
    };

    public SettingsControl(Settings s) {
      InitializeComponent();
      _s = s;

      // Initialize UI from settings
      EnableFlags.IsChecked = s.EnableFlags;
      EnableRadar.IsChecked = s.EnableRadar;
      DemoMode.IsChecked = s.DemoMode;
      Shape.SelectedIndex = s.Shape;
      RadarShape.SelectedIndex = s.RadarShape;

      // Headset selector: "Other" (no mask) plus the known headsets.
      Headset.Items.Add("Other");
      foreach (var hs in Headsets) Headset.Items.Add(hs.Name);
      Headset.SelectedItem = s.Headset;
      if (Headset.SelectedIndex < 0) Headset.SelectedIndex = 0;
      Headset.SelectionChanged += (_, __) => s.Headset = Headset.SelectedItem as string ?? "Other";
      RadarRange.Value = s.RadarRange;
      ScaleRadar.Value = s.ScaleRadar;
      RadarMaxOpacity.Value = s.RadarMaxOpacity;
      ScaleFlag.Value = s.ScaleFlag;
      FlagOpacity.Value = s.FlagOpacity;
      PosFlagX.Value = s.PosFlagx;
      PosFlagY.Value = s.PosFlagy;

      // Wire up event handlers
      EnableFlags.Checked += (_, __) => s.EnableFlags = true;
      EnableFlags.Unchecked += (_, __) => s.EnableFlags = false;
      EnableRadar.Checked += (_, __) => s.EnableRadar = true;
      EnableRadar.Unchecked += (_, __) => s.EnableRadar = false;
      DemoMode.Checked += (_, __) => s.DemoMode = true;
      DemoMode.Unchecked += (_, __) => s.DemoMode = false;
      Shape.SelectionChanged += (_, __) => s.Shape = (byte)Shape.SelectedIndex;
      RadarShape.SelectionChanged += (_, __) => s.RadarShape = (byte)RadarShape.SelectedIndex;

      // Sliders write straight to settings; the paired TextBoxes are two-way
      // bound to Slider.Value in XAML, so typing a number moves the slider (and
      // fires these handlers) and dragging updates the box -- no manual sync.
      RadarRange.ValueChanged += (_, __) => s.RadarRange = (float)RadarRange.Value;
      ScaleRadar.ValueChanged += (_, __) => s.ScaleRadar = (float)ScaleRadar.Value;
      RadarMaxOpacity.ValueChanged += (_, __) => s.RadarMaxOpacity = (float)RadarMaxOpacity.Value;
      ScaleFlag.ValueChanged += (_, __) => s.ScaleFlag = (float)ScaleFlag.Value;
      FlagOpacity.ValueChanged += (_, __) => s.FlagOpacity = (float)FlagOpacity.Value;
      PosFlagX.ValueChanged += (_, __) => s.PosFlagx = (float)PosFlagX.Value;
      PosFlagY.ValueChanged += (_, __) => s.PosFlagy = (float)PosFlagY.Value;

      // Run the animated preview only while the settings tab is visible.
      _previewTimer.Tick += (_, __) => RenderPreview();
      Loaded   += (_, __) => { _previewClock.Restart(); _previewTimer.Start(); };
      Unloaded += (_, __) => _previewTimer.Stop();
    }

    void RenderPreview() {
      double t = _previewClock.Elapsed.TotalSeconds;
      uint carCount = DemoData.Fill(t, _previewCars, out byte flags);
      DrawEye(LeftEye, flags, carCount, leftEye: true);
      DrawEye(RightEye, flags, carCount, leftEye: false);
    }

    // Horizontal parallax between the eyes: near cockpit geometry is shifted in
    // opposite directions for each eye so the preview reads as a real stereo pair
    // (the screen-space overlay HUD is NOT parallaxed -- the layer emits it at a
    // fixed per-eye position, world-unlocked).
    const double kEyeParallax = 0.05;

    BitmapImage _cockpit;      // cached PNG; null once we know none is present
    bool _cockpitTried;        // load is attempted exactly once

    // Load the cockpit PNG embedded in the plugin DLL (single-file deploy, no
    // loose asset). Frozen so it can be reused across the animated preview's
    // redraws. Returns null if no cockpit resource is bundled.
    BitmapImage CockpitImage() {
      if (_cockpitTried) return _cockpit;
      _cockpitTried = true;
      try {
        var asm = typeof(SettingsControl).Assembly;
        string name = null;
        foreach (var n in asm.GetManifestResourceNames())
          if (n.EndsWith("cockpit.png", StringComparison.OrdinalIgnoreCase)) { name = n; break; }
        if (name != null) {
          using (var s = asm.GetManifestResourceStream(name)) {
            var bi = new BitmapImage();
            bi.BeginInit();
            bi.CacheOption = BitmapCacheOption.OnLoad;   // decode now, then release the stream
            bi.StreamSource = s;
            bi.EndInit();
            bi.Freeze();
            _cockpit = bi;
          }
        }
      } catch { _cockpit = null; }
      return _cockpit;
    }

    void DrawEye(Canvas c, byte flags, uint carCount, bool leftEye) {
      c.Children.Clear();
      c.ClipToBounds = true;    // keep the parallax-shifted cockpit inside the eye

      DrawCockpit(c, leftEye);  // background frame, behind all overlay content

      if (_s.EnableFlags && flags != 0) {
        foreach (byte bit in FlagPriority) {
          if ((flags & bit) != 0) {
            byte a = (byte)(_s.FlagOpacity * 255);
            // Meatball flag: black flag with an orange centre disc (mirror overlay.cpp).
            bool meatball = bit == 64;
            uint baseCol = ((meatball ? 0xFF101010u : FlagColor(bit)) & 0x00FFFFFFu) | ((uint)a << 24);
            Color fc = FromArgb(baseCol);
            Color dotc = FromArgb((0xFFFF8000u & 0x00FFFFFFu) | ((uint)a << 24));
            if (_s.Shape == 0) {
              // Bar: full-height vertical bar down the outer edge, pulled in from
              // the extreme edge so it stays inside the headset visible area.
              const double kFlagEdge = 0.82;
              double w = 0.05 * _s.ScaleFlag;
              double ex = leftEye ? -kFlagEdge : kFlagEdge;
              AddRect(c, ex, 0, w, 1.0, fc);
              if (meatball) AddEllipse(c, ex, 0, 2.0 * w, 2.0 * w, dotc);
            } else {
              // Other shapes: marker mirrored to the outer edge of each eye.
              double sz = 0.15 * _s.ScaleFlag;
              double ex = leftEye ? -Math.Abs(_s.PosFlagx) : Math.Abs(_s.PosFlagx);
              DrawFlagShape(c, _s.Shape, ex, _s.PosFlagy, sz, fc);
              if (meatball) {
                // Centre the disc on the shape's centroid (triangle's apex-up
                // centroid sits sz/3 below centre; every other shape is centred).
                double dcy = _s.PosFlagy - (_s.Shape == 4 ? kTriHalf * sz / 3.0 : 0.0);
                AddEllipse(c, ex, dcy, 0.45 * sz, 0.45 * sz, dotc);
              }
            }
            break;
          }
        }
      }

      if (_s.EnableRadar) {
        double range = _s.RadarRange > 0 ? _s.RadarRange : 1;
        for (uint i = 0; i < carCount; i++) {
          CarBlip car = _previewCars[i];
          if (car.Side == 3 || car.Side == 0) continue;    // never draw ahead / none
          // Per-eye stereo: left cars only in left eye, right cars only in right
          // eye, cars behind in both so they are always visible.
          if (car.Side == 1 && !leftEye) continue;
          if (car.Side == 2 && leftEye) continue;

          double tt = car.Distance / range; if (tt > 1) tt = 1;
          double closeness = 1 - tt;
          double bearing = Math.Atan2(car.Rel.X, -car.Rel.Y);
          double halfW = 0.03 * _s.ScaleRadar;  // uniform; threat shown by colour/opacity

          // Shades of red: closer = brighter + more opaque, farther = dimmer + fainter.
          double alpha = _s.RadarMaxOpacity * (0.15 + 0.85 * closeness);
          double bright = 0.35 + 0.65 * closeness;
          uint col = Scale(0xFFFF0000u, bright, alpha);

          // Both shapes ride a circle centered on the lens middle at the car's
          // bearing; radius scales with ScaleRadar, clamped so back + outer stay
          // on-lens (mirrors overlay.cpp). Canvas y is flipped vs the layer NDC,
          // so the arrow rotation is PI+bearing (car ignores angle).
          double radius = Math.Min(0.8 * _s.ScaleRadar, 0.85);
          double u =  radius * Math.Sin(bearing);
          double v = -radius * Math.Cos(bearing);
          double angle = Math.PI + bearing;
          DrawRadarShape(c, _s.RadarShape, u, v, halfW, angle, FromArgb(col));
        }
      }

      DrawMask(c);  // dim the periphery outside the selected headset's visible area
    }

    // Static cockpit silhouette so the preview reads like an in-headset view.
    // Drawn behind the overlay content, with a small per-eye horizontal parallax
    // (dx) so the two eyes are slightly offset like a real stereo pair. Purely a
    // preview aid -- not part of the shared-memory contract or the layer render.
    // (Vector art rather than a bundled bitmap; swap in an <Image> later if wanted.)
    void DrawCockpit(Canvas c, bool leftEye) {
      double dx = leftEye ? kEyeParallax : -kEyeParallax;

      // Prefer a real cockpit PNG (dropped next to the plugin DLL); fall back to
      // the vector silhouette when absent. The PNG is shifted horizontally by the
      // per-eye parallax, exactly like the vector art.
      var png = CockpitImage();
      if (png != null) {
        double iw = c.Width, ih = c.Height;
        var img = new Image { Source = png, Width = iw, Height = ih, Stretch = Stretch.Fill };
        Canvas.SetLeft(img, dx * iw / 2);   // NDC dx -> px (NDC width 2 spans the canvas)
        Canvas.SetTop(img, 0);
        c.Children.Add(img);
        return;
      }

      var dash   = Color.FromArgb(0xFF, 0x24, 0x26, 0x2B);  // dashboard body
      var trim   = Color.FromArgb(0xFF, 0x3A, 0x3D, 0x45);  // wheel rim / mirror
      var pillar = Color.FromArgb(0xFF, 0x18, 0x19, 0x1D);  // A-pillars

      // Dashboard: fills the lower field, dipping in the center for the gauges.
      AddPolygon(c, dx, dash, new[] {
        new Point(-1.0, -1.0), new Point(1.0, -1.0),
        new Point(1.0, -0.30), new Point(0.55, -0.42), new Point(0.20, -0.55),
        new Point(-0.20, -0.55), new Point(-0.55, -0.42), new Point(-1.0, -0.30),
      });

      // A-pillars framing the windshield at the top corners.
      AddPolygon(c, dx, pillar, new[] {
        new Point(-1.0, 1.0), new Point(-0.60, 1.0), new Point(-1.0, -0.05) });
      AddPolygon(c, dx, pillar, new[] {
        new Point(1.0, 1.0), new Point(0.60, 1.0), new Point(1.0, -0.05) });

      // Steering-wheel rim rising from the bottom center (only the top arc shows).
      AddEllipseStroke(c, dx, -1.15, 0.52, 0.52, trim, 0.03);

      // Rear-view mirror at top center.
      AddRect(c, dx, 0.82, 0.16, 0.05, trim);
    }

    // Filled polygon in NDC (u,v; y up), shifted horizontally by dx for parallax.
    static void AddPolygon(Canvas c, double dx, Color col, Point[] ndc) {
      double W = c.Width, H = c.Height;
      var poly = new Polygon { Fill = new SolidColorBrush(col) };
      foreach (var p in ndc)
        poly.Points.Add(new Point(((p.X + dx) + 1) / 2 * W, (1 - p.Y) / 2 * H));
      c.Children.Add(poly);
    }

    // Unfilled ellipse (stroke only) centered at NDC (u,v); thickNdc in NDC units.
    static void AddEllipseStroke(Canvas c, double u, double v, double hw, double hh, Color col, double thickNdc) {
      double W = c.Width, H = c.Height;
      double cx = (u + 1) / 2 * W, cy = (1 - v) / 2 * H, pw = hw * W, ph = hh * H;
      var e = new Ellipse {
        Width = pw, Height = ph,
        Stroke = new SolidColorBrush(col), StrokeThickness = thickNdc * W,
      };
      Canvas.SetLeft(e, cx - pw / 2);
      Canvas.SetTop(e, cy - ph / 2);
      c.Children.Add(e);
    }

    // Approximate the headset's visible area by dimming the periphery: content
    // that falls in the dark border would sit near/beyond the lens edge in VR.
    void DrawMask(Canvas c) {
      double hv = -1, vv = -1;
      foreach (var hs in Headsets)
        if (hs.Name == _s.Headset) { hv = hs.H; vv = hs.V; break; }
      if (hv < 0) return;  // "Other"/unknown: no mask

      double W = c.Width, H = c.Height;
      double mx = (1 - hv) / 2 * W;   // horizontal margin each side
      double my = (1 - vv) / 2 * H;   // vertical margin top/bottom
      var b = new SolidColorBrush(Color.FromArgb(0x99, 0, 0, 0));
      AddBand(c, b, 0, 0, W, my);                     // top
      AddBand(c, b, 0, H - my, W, my);                // bottom
      AddBand(c, b, 0, my, mx, H - 2 * my);           // left
      AddBand(c, b, W - mx, my, mx, H - 2 * my);      // right
    }

    static void AddBand(Canvas c, Brush b, double x, double y, double w, double h) {
      if (w <= 0 || h <= 0) return;
      var r = new Rectangle { Width = w, Height = h, Fill = b };
      Canvas.SetLeft(r, x);
      Canvas.SetTop(r, y);
      c.Children.Add(r);
    }

    // Non-bar flag shapes scaled to equal visual AREA (reference: circle radius
    // sz) so swapping shape never changes apparent size. Mirror overlay.cpp.
    const double kSquareHalf = 0.8862269;  // sqrt(pi)/2
    const double kTriHalf    = 1.2533141;  // sqrt(pi/2)
    const double kRectHalfW  = 1.1441037;  // sqrt(pi/2.4)
    const double kRectHalfH  = 0.6864622;  // 0.6 * kRectHalfW

    // Flag shape emitters (NDC centre u,v; sz = half-extent base). Mirror overlay.cpp.
    void DrawFlagShape(Canvas c, byte shape, double u, double v, double sz, Color col) {
      switch (shape) {
        case 0: AddRect(c, u, v, sz, 0.35 * sz, col); break;                       // bar (not area-matched)
        case 1: AddRect(c, u, v, kRectHalfW * sz, kRectHalfH * sz, col); break;    // rect
        case 2: AddRect(c, u, v, kSquareHalf * sz, kSquareHalf * sz, col); break;  // square
        case 3: AddEllipse(c, u, v, sz, sz, col); break;                           // circle (reference)
        case 4: AddTriangle(c, u, v, kTriHalf * sz, kTriHalf * sz, 0, col); break; // triangle (points up)
        default: AddRect(c, u, v, kSquareHalf * sz, kSquareHalf * sz, col); break;
      }
    }

    // Radar shape emitters. car = small upright rectangle marker on the arc;
    // arrow = triangle on the arc rotated to point at the car.
    void DrawRadarShape(Canvas c, byte shape, double u, double v, double halfW, double angle, Color col) {
      if (shape == 1) AddTriangle(c, u, v, 1.2 * halfW, 1.7 * halfW, angle, col);    // arrow
      else            AddRect(c, u, v, halfW, 1.8 * halfW, col);                     // car (vertical rect)
    }

    static void AddRect(Canvas c, double u, double v, double hw, double hh, Color col) {
      double W = c.Width, H = c.Height;
      double cx = (u + 1) / 2 * W, cy = (1 - v) / 2 * H;
      double pw = hw * W, ph = hh * H;
      var r = new Rectangle { Width = pw, Height = ph, Fill = new SolidColorBrush(col) };
      Canvas.SetLeft(r, cx - pw / 2);
      Canvas.SetTop(r, cy - ph / 2);
      c.Children.Add(r);
    }

    static void AddEllipse(Canvas c, double u, double v, double hw, double hh, Color col) {
      double W = c.Width, H = c.Height;
      double cx = (u + 1) / 2 * W, cy = (1 - v) / 2 * H;
      double pw = hw * W, ph = hh * H;
      var e = new Ellipse { Width = pw, Height = ph, Fill = new SolidColorBrush(col) };
      Canvas.SetLeft(e, cx - pw / 2);
      Canvas.SetTop(e, cy - ph / 2);
      c.Children.Add(e);
    }

    // Isoceles triangle, apex pointing "up" in NDC then rotated by `angle` radians
    // (clockwise from up, matching a bearing measured as atan2(x,-y)).
    static void AddTriangle(Canvas c, double u, double v, double hw, double hh, double angle, Color col) {
      double W = c.Width, H = c.Height;
      double cx = (u + 1) / 2 * W, cy = (1 - v) / 2 * H;
      double pw = hw * W, ph = hh * H;
      // Local apex up, base at bottom (canvas y-down so apex is negative y).
      var pts = new[] {
        new Point(0, -ph),
        new Point(-pw, ph),
        new Point(pw, ph),
      };
      double sa = Math.Sin(angle), ca = Math.Cos(angle);
      var poly = new Polygon { Fill = new SolidColorBrush(col) };
      foreach (var p in pts) {
        // Rotate clockwise by `angle` about the centre.
        double rx = p.X * ca + p.Y * sa;
        double ry = -p.X * sa + p.Y * ca;
        poly.Points.Add(new Point(cx + rx, cy + ry));
      }
      c.Children.Add(poly);
    }

    static Color FromArgb(uint c) =>
      Color.FromArgb((byte)(c >> 24), (byte)(c >> 16), (byte)(c >> 8), (byte)c);

    // Scale an 0xAARRGGBB colour's RGB by `bright` and override alpha with `alpha`.
    static uint Scale(uint c, double bright, double alpha) {
      byte r = (byte)Math.Min(255, ((c >> 16) & 0xFF) * bright);
      byte g = (byte)Math.Min(255, ((c >> 8) & 0xFF) * bright);
      byte b = (byte)Math.Min(255, (c & 0xFF) * bright);
      byte a = (byte)Math.Min(255, Math.Max(0, alpha * 255));
      return ((uint)a << 24) | ((uint)r << 16) | ((uint)g << 8) | b;
    }

    static uint FlagColor(byte bit) {  // mirror of overlay.cpp FlagColor
      switch (bit) {
        case 16: return 0xFFFF2020u; // red
        case 64: return 0xFFFF8000u; // meatball/orange
        case 32: return 0xFF101010u; // black
        case 4:  return 0xFF2060FFu; // blue
        case 2:  return 0xFFFFE000u; // yellow
        case 8:  return 0xFFF0F0F0u; // white
        case 1:  return 0xFF20D040u; // green
        default: return 0x00000000u;
      }
    }

  }
}
