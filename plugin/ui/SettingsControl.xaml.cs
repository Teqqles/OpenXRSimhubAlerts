// plugin/ui/SettingsControl.xaml.cs
using System;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
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

      UpdateValueLabels();

      // Wire up event handlers
      EnableFlags.Checked += (_, __) => s.EnableFlags = true;
      EnableFlags.Unchecked += (_, __) => s.EnableFlags = false;
      EnableRadar.Checked += (_, __) => s.EnableRadar = true;
      EnableRadar.Unchecked += (_, __) => s.EnableRadar = false;
      DemoMode.Checked += (_, __) => s.DemoMode = true;
      DemoMode.Unchecked += (_, __) => s.DemoMode = false;
      Shape.SelectionChanged += (_, __) => s.Shape = (byte)Shape.SelectedIndex;
      RadarShape.SelectionChanged += (_, __) => s.RadarShape = (byte)RadarShape.SelectedIndex;

      RadarRange.ValueChanged += (_, __) => { s.RadarRange = (float)RadarRange.Value; UpdateValueLabels(); };
      ScaleRadar.ValueChanged += (_, __) => { s.ScaleRadar = (float)ScaleRadar.Value; UpdateValueLabels(); };
      RadarMaxOpacity.ValueChanged += (_, __) => { s.RadarMaxOpacity = (float)RadarMaxOpacity.Value; UpdateValueLabels(); };
      ScaleFlag.ValueChanged += (_, __) => { s.ScaleFlag = (float)ScaleFlag.Value; UpdateValueLabels(); };
      FlagOpacity.ValueChanged += (_, __) => { s.FlagOpacity = (float)FlagOpacity.Value; UpdateValueLabels(); };
      PosFlagX.ValueChanged += (_, __) => { s.PosFlagx = (float)PosFlagX.Value; UpdateValueLabels(); };
      PosFlagY.ValueChanged += (_, __) => { s.PosFlagy = (float)PosFlagY.Value; UpdateValueLabels(); };

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

    void DrawEye(Canvas c, byte flags, uint carCount, bool leftEye) {
      c.Children.Clear();

      if (_s.EnableFlags && flags != 0) {
        foreach (byte bit in FlagPriority) {
          if ((flags & bit) != 0) {
            uint col = (FlagColor(bit) & 0x00FFFFFFu) | ((uint)(byte)(_s.FlagOpacity * 255) << 24);
            double sz = 0.15 * _s.ScaleFlag;               // matches overlay.cpp flag base size
            DrawFlagShape(c, _s.Shape, _s.PosFlagx, _s.PosFlagy, sz, FromArgb(col));
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
          double radius = 0.5 + 0.4 * tt;
          double u = radius * Math.Sin(bearing);
          double v = -0.6 + radius * (Math.Cos(bearing) * 0.2);
          double sz = ((car.Flags & 1) != 0 ? 0.05 : 0.03) * _s.ScaleRadar;

          // Opacity + brightness rise as the car gets closer, up to the ceiling.
          double alpha = _s.RadarMaxOpacity * (0.35 + 0.65 * closeness);
          double bright = 0.5 + 0.5 * closeness;
          uint baseCol = car.Side == 4 ? 0xFFFFFFFFu : 0xFFFFC000u;
          uint col = Scale(baseCol, bright, alpha);
          DrawRadarShape(c, _s.RadarShape, u, v, sz, bearing, FromArgb(col));
        }
      }

      DrawMask(c);  // dim the periphery outside the selected headset's visible area
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

    // Flag shape emitters (NDC centre u,v; sz = half-extent base). Mirror overlay.cpp.
    void DrawFlagShape(Canvas c, byte shape, double u, double v, double sz, Color col) {
      switch (shape) {
        case 0: AddEllipse(c, u, v, 0.4 * sz, 0.4 * sz, col); break;         // dot
        case 1: AddRect(c, u, v, sz, 0.35 * sz, col); break;                 // bar
        case 2: AddRect(c, u, v, sz, 0.6 * sz, col); break;                  // rect
        case 3: AddRect(c, u, v, sz, sz, col); break;                        // square
        case 4: AddEllipse(c, u, v, sz, sz, col); break;                     // circle
        case 5: AddTriangle(c, u, v, sz, sz, 0, col); break;                 // triangle (points up)
        default: AddRect(c, u, v, sz, sz, col); break;
      }
    }

    // Radar shape emitters. car = vertical bar; arrow = triangle rotated to point
    // outward along the car's bearing.
    void DrawRadarShape(Canvas c, byte shape, double u, double v, double sz, double bearing, Color col) {
      if (shape == 1) AddTriangle(c, u, v, sz, 1.4 * sz, bearing, col);      // arrow
      else            AddRect(c, u, v, 0.6 * sz, 1.4 * sz, col);             // car (vertical bar)
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

    void UpdateValueLabels() {
      RadarRangeValue.Text = RadarRange.Value.ToString("F0");
      ScaleRadarValue.Text = ScaleRadar.Value.ToString("F1");
      RadarMaxOpacityValue.Text = RadarMaxOpacity.Value.ToString("F2");
      ScaleFlagValue.Text = ScaleFlag.Value.ToString("F1");
      FlagOpacityValue.Text = FlagOpacity.Value.ToString("F2");
      PosFlagXValue.Text = PosFlagX.Value.ToString("F2");
      PosFlagYValue.Text = PosFlagY.Value.ToString("F2");
    }
  }
}
