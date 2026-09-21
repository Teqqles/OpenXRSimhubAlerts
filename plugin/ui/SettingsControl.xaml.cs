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
    // layer would render. The 2D placement math below mirrors overlay.cpp
    // (FlagColor, flag u/v/size, radar ring) -- keep the two in sync.
    readonly DispatcherTimer _previewTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(50) };
    readonly System.Diagnostics.Stopwatch _previewClock = new System.Diagnostics.Stopwatch();
    readonly CarBlip[] _previewCars = new CarBlip[ShmContract.MaxCars];
    static readonly byte[] FlagPriority = { 16, 64, 32, 4, 2, 8, 1 }; // Red,Meatball,Black,Blue,Yellow,White,Green

    public SettingsControl(Settings s) {
      InitializeComponent();
      _s = s;

      // Initialize UI from settings
      EnableFlags.IsChecked = s.EnableFlags;
      EnableRadar.IsChecked = s.EnableRadar;
      DemoMode.IsChecked = s.DemoMode;
      Shape.SelectedIndex = s.Shape;
      RadarRange.Value = s.RadarRange;
      ScaleFlag.Value = s.ScaleFlag;
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

      RadarRange.ValueChanged += (_, __) => {
        s.RadarRange = (float)RadarRange.Value;
        UpdateValueLabels();
      };

      ScaleFlag.ValueChanged += (_, __) => {
        s.ScaleFlag = (float)ScaleFlag.Value;
        UpdateValueLabels();
      };

      PosFlagX.ValueChanged += (_, __) => {
        s.PosFlagx = (float)PosFlagX.Value;
        UpdateValueLabels();
      };

      PosFlagY.ValueChanged += (_, __) => {
        s.PosFlagy = (float)PosFlagY.Value;
        UpdateValueLabels();
      };

      // Run the animated preview only while the settings tab is visible.
      _previewTimer.Tick += (_, __) => RenderPreview();
      Loaded   += (_, __) => { _previewClock.Restart(); _previewTimer.Start(); };
      Unloaded += (_, __) => _previewTimer.Stop();
    }

    void RenderPreview() {
      double t = _previewClock.Elapsed.TotalSeconds;
      uint carCount = DemoData.Fill(t, _previewCars, out byte flags);
      DrawEye(LeftEye, flags, carCount);
      DrawEye(RightEye, flags, carCount);
    }

    void DrawEye(Canvas c, byte flags, uint carCount) {
      c.Children.Clear();

      if (_s.EnableFlags && flags != 0) {
        foreach (byte bit in FlagPriority) {
          if ((flags & bit) != 0) {
            double s = 0.15 * _s.ScaleFlag;                 // matches overlay.cpp flag size
            AddQuad(c, _s.PosFlagx, _s.PosFlagy, s, s, FromArgb(FlagColor(bit)));
            break;
          }
        }
      }

      if (_s.EnableRadar) {
        double range = _s.RadarRange > 0 ? _s.RadarRange : 1;
        for (uint i = 0; i < carCount; i++) {
          CarBlip car = _previewCars[i];
          if (car.Side == 3 || car.Side == 0) continue;      // never draw ahead / none
          double tt = car.Distance / range; if (tt > 1) tt = 1;
          double bearing = Math.Atan2(car.Rel.X, -car.Rel.Y);
          double radius = 0.5 + 0.4 * tt;
          double u = radius * Math.Sin(bearing);
          double v = -0.6 + radius * (Math.Cos(bearing) * 0.2);
          double sz = (car.Flags & 1) != 0 ? 0.05 : 0.03;
          uint col = car.Side == 4 ? 0xFFFFFFFFu : 0xFFFFC000u;
          AddQuad(c, u, v, sz, sz, FromArgb(col));
        }
      }
    }

    // Map an NDC-space quad (u,v centre; w,h half-extents; y up) onto a canvas.
    static void AddQuad(Canvas c, double u, double v, double hw, double hh, Color col) {
      double W = c.Width, H = c.Height;
      double cx = (u + 1) / 2 * W;
      double cy = (1 - v) / 2 * H;         // NDC y-up -> canvas y-down
      double pw = hw * W, ph = hh * H;
      var r = new Rectangle { Width = pw, Height = ph, Fill = new SolidColorBrush(col) };
      Canvas.SetLeft(r, cx - pw / 2);
      Canvas.SetTop(r, cy - ph / 2);
      c.Children.Add(r);
    }

    static Color FromArgb(uint c) =>
      Color.FromArgb((byte)(c >> 24), (byte)(c >> 16), (byte)(c >> 8), (byte)c);

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
      ScaleFlagValue.Text = ScaleFlag.Value.ToString("F1");
      PosFlagXValue.Text = PosFlagX.Value.ToString("F2");
      PosFlagYValue.Text = PosFlagY.Value.ToString("F2");
    }
  }
}
