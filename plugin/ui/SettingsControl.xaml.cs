// plugin/ui/SettingsControl.xaml.cs
using System;
using System.Collections.Generic;
using System.Linq;
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

    // Animated preview: demo telemetry goes through the same OverlayComposer the
    // plugin publishes to the layer, so each eye draws the element list the
    // headset shows.
    readonly DispatcherTimer _previewTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(50) };
    readonly System.Diagnostics.Stopwatch _previewClock = new System.Diagnostics.Stopwatch();
    readonly CarBlip[] _previewCars = new CarBlip[RadarCalculator.MaxCars];
    readonly ShiftLights _previewShift = new ShiftLights();
    readonly DriverAids _previewAids = new DriverAids();
    DataBlock _previewBlock;

    // RefreshRate ComboBox items, in display order (index != contract value).
    static readonly RefreshMode[] RefreshRateOrder = {
      RefreshMode.Auto, RefreshMode.Unlimited, RefreshMode.Fps60, RefreshMode.Fps30,
      RefreshMode.Fps15, RefreshMode.Fps10, RefreshMode.Fps5, RefreshMode.Fps1,
    };

    public SettingsControl(Settings s) {
      InitializeComponent();
      _s = s;

      // Headset selector: "Other" (whole image) plus the known headsets.
      Headset.Items.Add("Other");
      foreach (var hs in HeadsetMasks.Presets) Headset.Items.Add(hs.Name);

      ShowSettings();
      WireHandlers(s);

      ExportSettings.Click += (_, __) => Export();
      ImportSettings.Click += (_, __) => Import();

      // Animate only while the Preview tab is open; the tab control unloads the
      // content of tabs that are not selected.
      _previewTimer.Tick += (_, __) => RenderPreview();
      PreviewTab.Loaded   += (_, __) => { _previewClock.Restart(); _previewTimer.Start(); };
      PreviewTab.Unloaded += (_, __) => _previewTimer.Stop();
    }

    // Sets every control from the settings: at start-up and after an import.
    void ShowSettings() {
      var s = _s;
      EnableFlags.IsChecked = s.EnableFlags;
      EnableRadar.IsChecked = s.EnableRadar;
      DemoMode.IsChecked = s.DemoMode;
      Shape.SelectedIndex = s.Shape;
      RadarShape.SelectedIndex = s.RadarShape;
      RefreshRate.SelectedIndex = Math.Max(0, Array.IndexOf(RefreshRateOrder, s.RefreshMode));
      Headset.SelectedItem = s.Headset;
      if (Headset.SelectedIndex < 0) Headset.SelectedIndex = 0;
      RadarRange.Value = s.RadarRange;
      ScaleRadar.Value = s.ScaleRadar;
      RadarMaxOpacity.Value = s.RadarMaxOpacity;
      ScaleFlag.Value = s.ScaleFlag;
      FlagOpacity.Value = s.FlagOpacity;
      PosFlagX.Value = s.PosFlagx;
      PosFlagY.Value = s.PosFlagy;
      EnableShiftLights.IsChecked = s.EnableShiftLights;
      ShowUnlitLights.IsChecked = s.ShowUnlitLights;
      ShiftLightCount.Value = ShiftLights.LightCount(s.ShiftLightCount);
      ShiftGlow.Value = s.ShiftGlow;
      ScaleShift.Value = s.ScaleShift;
      ShiftOpacity.Value = s.ShiftOpacity;
      PosShiftX.Value = s.PosShiftx;
      PosShiftY.Value = s.PosShifty;
      EnableAbs.IsChecked = s.EnableAbs;
      EnableTc.IsChecked = s.EnableTc;
      EnableDrs.IsChecked = s.EnableDrs;
      ScaleAids.Value = s.ScaleAids;
      AidsOpacity.Value = s.AidsOpacity;
      PosAidsX.Value = s.PosAidsx;
      PosAidsY.Value = s.PosAidsy;
    }

    // Each control writes straight to the settings when it changes.
    void WireHandlers(Settings s) {
      Headset.SelectionChanged += (_, __) => s.Headset = Headset.SelectedItem as string ?? "Other";
      EnableFlags.Checked += (_, __) => s.EnableFlags = true;
      EnableFlags.Unchecked += (_, __) => s.EnableFlags = false;
      EnableRadar.Checked += (_, __) => s.EnableRadar = true;
      EnableRadar.Unchecked += (_, __) => s.EnableRadar = false;
      DemoMode.Checked += (_, __) => s.DemoMode = true;
      DemoMode.Unchecked += (_, __) => s.DemoMode = false;
      Shape.SelectionChanged += (_, __) => s.Shape = (byte)Shape.SelectedIndex;
      RadarShape.SelectionChanged += (_, __) => s.RadarShape = (byte)RadarShape.SelectedIndex;
      RefreshRate.SelectionChanged += (_, __) => s.RefreshMode = RefreshRateOrder[RefreshRate.SelectedIndex];
      EnableShiftLights.Checked += (_, __) => s.EnableShiftLights = true;
      EnableShiftLights.Unchecked += (_, __) => s.EnableShiftLights = false;
      ShowUnlitLights.Checked += (_, __) => s.ShowUnlitLights = true;
      ShowUnlitLights.Unchecked += (_, __) => s.ShowUnlitLights = false;
      EnableAbs.Checked += (_, __) => s.EnableAbs = true;
      EnableAbs.Unchecked += (_, __) => s.EnableAbs = false;
      EnableTc.Checked += (_, __) => s.EnableTc = true;
      EnableTc.Unchecked += (_, __) => s.EnableTc = false;
      EnableDrs.Checked += (_, __) => s.EnableDrs = true;
      EnableDrs.Unchecked += (_, __) => s.EnableDrs = false;

      // Sliders write straight to settings; the paired TextBoxes are two-way
      // bound to Slider.Value in XAML, so typing a number moves the slider (and
      // fires these handlers) and dragging updates the box without manual sync.
      RadarRange.ValueChanged += (_, __) => s.RadarRange = (float)RadarRange.Value;
      ScaleRadar.ValueChanged += (_, __) => s.ScaleRadar = (float)ScaleRadar.Value;
      RadarMaxOpacity.ValueChanged += (_, __) => s.RadarMaxOpacity = (float)RadarMaxOpacity.Value;
      ScaleFlag.ValueChanged += (_, __) => s.ScaleFlag = (float)ScaleFlag.Value;
      FlagOpacity.ValueChanged += (_, __) => s.FlagOpacity = (float)FlagOpacity.Value;
      PosFlagX.ValueChanged += (_, __) => s.PosFlagx = (float)PosFlagX.Value;
      PosFlagY.ValueChanged += (_, __) => s.PosFlagy = (float)PosFlagY.Value;
      ShiftLightCount.ValueChanged += (_, __) => s.ShiftLightCount = (int)Math.Round(ShiftLightCount.Value);
      ShiftGlow.ValueChanged += (_, __) => s.ShiftGlow = (float)ShiftGlow.Value;
      ScaleShift.ValueChanged += (_, __) => s.ScaleShift = (float)ScaleShift.Value;
      ShiftOpacity.ValueChanged += (_, __) => s.ShiftOpacity = (float)ShiftOpacity.Value;
      PosShiftX.ValueChanged += (_, __) => s.PosShiftx = (float)PosShiftX.Value;
      PosShiftY.ValueChanged += (_, __) => s.PosShifty = (float)PosShiftY.Value;
      ScaleAids.ValueChanged += (_, __) => s.ScaleAids = (float)ScaleAids.Value;
      AidsOpacity.ValueChanged += (_, __) => s.AidsOpacity = (float)AidsOpacity.Value;
      PosAidsX.ValueChanged += (_, __) => s.PosAidsx = (float)PosAidsX.Value;
      PosAidsY.ValueChanged += (_, __) => s.PosAidsy = (float)PosAidsY.Value;
    }

    const string ExportFilter = "Settings (*.json)|*.json|All files (*.*)|*.*";

    void Export() {
      var dialog = new Microsoft.Win32.SaveFileDialog {
        Title = "Export OpenXR SimHub Alerts settings",
        Filter = ExportFilter,
        FileName = "OpenXRSimHubAlerts-settings.json",
      };
      if (dialog.ShowDialog() != true) return;
      try {
        System.IO.File.WriteAllText(dialog.FileName, SettingsStore.Serialize(_s));
      } catch (Exception ex) when (ex is System.IO.IOException || ex is UnauthorizedAccessException) {
        MessageBox.Show("Could not export the settings:\n" + ex.Message, "Export settings",
                        MessageBoxButton.OK, MessageBoxImage.Warning);
      }
    }

    // A file that is not valid settings changes nothing. A valid one replaces
    // every setting in the shared Settings object, so the overlay and preview
    // pick it up at once, and autosave writes it to the settings file.
    void Import() {
      var dialog = new Microsoft.Win32.OpenFileDialog {
        Title = "Import OpenXR SimHub Alerts settings",
        Filter = ExportFilter,
      };
      if (dialog.ShowDialog() != true) return;
      string json;
      try {
        json = System.IO.File.ReadAllText(dialog.FileName);
      } catch (Exception ex) when (ex is System.IO.IOException || ex is UnauthorizedAccessException) {
        MessageBox.Show("Could not read the file:\n" + ex.Message, "Import settings",
                        MessageBoxButton.OK, MessageBoxImage.Warning);
        return;
      }
      if (!SettingsStore.TryDeserialize(json, out Settings imported, out string error)) {
        MessageBox.Show("This is not an OpenXR SimHub Alerts settings file:\n" + error, "Import settings",
                        MessageBoxButton.OK, MessageBoxImage.Warning);
        return;
      }
      SettingsStore.CopyInto(imported, _s);
      ShowSettings();
    }

    void RenderPreview() {
      double t = _previewClock.Elapsed.TotalSeconds;
      uint carCount = DemoData.Fill(t, _previewCars, out byte flags);
      ShiftState shift = _previewShift.Update(DemoData.Shift(t), t, ShiftLights.LightCount(_s.ShiftLightCount));
      DriverAidState aids = _previewAids.Update(DemoData.Aids(t), t);
      OverlayComposer.Compose(_s, flags, _previewCars, carCount, shift, aids, ref _previewBlock);
      var elements = _previewBlock.Elements
        .Take((int)_previewBlock.ElementCount)
        .OrderBy(e => e.Priority)   // stable: matches the layer's draw order
        .ToArray();
      DrawEye(LeftEye, elements, Eyes.Left);
      DrawEye(RightEye, elements, Eyes.Right);
      CropToHeadset(LeftEyeView, LeftEye, leftEye: true);
      CropToHeadset(RightEyeView, RightEye, leftEye: false);
    }

    // Horizontal parallax between the eyes: near cockpit geometry is shifted in
    // opposite directions for each eye so the preview reads as a real stereo pair
    // (the screen-space overlay HUD is NOT parallaxed; the layer emits it at a
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

    void DrawEye(Canvas c, Element[] elements, Eyes eye) {
      c.Children.Clear();
      c.ClipToBounds = true;    // keep the parallax-shifted cockpit inside the eye

      DrawCockpit(c, eye == Eyes.Left);  // background frame, behind all overlay content

      foreach (var e in elements) {
        if ((e.Eyes & eye) == 0) continue;
        var col = FromArgb(e.Color);
        switch (e.Kind) {
          case ElementKind.Rect:     AddRect(c, e.U, e.V, e.HalfW, e.HalfH, col); break;
          case ElementKind.Ellipse:  AddEllipse(c, e.U, e.V, e.HalfW, e.HalfH, col); break;
          // Canvas y points down, which flips the direction of rotation.
          case ElementKind.Triangle: AddTriangle(c, e.U, e.V, e.HalfW, e.HalfH, -e.Angle, col); break;
          case ElementKind.Glow:     AddGlow(c, e.U, e.V, e.HalfW, e.HalfH, col); break;
          case ElementKind.Text:     AddText(c, e, col); break;
          case ElementKind.Icon:     AddIcon(c, e, col); break;
        }
      }

    }

    // Cached per-icon bitmap: decoded once and frozen, then reused for every
    // draw and every eye, keyed by IconId.
    readonly Dictionary<IconId, BitmapImage> _iconCache = new Dictionary<IconId, BitmapImage>();

    BitmapImage IconImage(IconId id) {
      if (_iconCache.TryGetValue(id, out var cached)) return cached;
      BitmapImage bi;
      using (var s = Icons.Open(id)) {
        bi = new BitmapImage();
        bi.BeginInit();
        bi.CacheOption = BitmapCacheOption.OnLoad;   // decode now, then release the stream
        bi.StreamSource = s;
        bi.EndInit();
        bi.Freeze();
      }
      _iconCache[id] = bi;
      return bi;
    }

    // One glyph (see ElementKind.Text): U is the pen origin, V the baseline, HalfH
    // the font size (em) in NDC. Skips characters the overlay font lacks.
    static void AddText(Canvas c, Element e, Color col) {
      var face = OverlayFont.Face;
      if (face == null || !face.CharacterToGlyphMap.TryGetValue((char)e.Ref, out ushort glyph)) return;

      double W = c.Width, H = c.Height;
      double emPx = e.HalfH * H / 2;
      double x = (e.U + 1) / 2 * W, y = (1 - e.V) / 2 * H;

      var glyphRun = new GlyphRun(face, 0, false, emPx, 1f,
        new[] { glyph }, new Point(x, y), new[] { face.AdvanceWidths[glyph] * emPx },
        null, null, null, null, null, null);
      var path = new Path { Data = glyphRun.BuildGeometry(), Fill = new SolidColorBrush(col) };
      c.Children.Add(path);
    }

    // Icon (see ElementKind.Icon): fills U +/- HalfW, V +/- HalfH, tinted with the
    // element colour through the icon PNG's alpha (its shape) as an opacity mask.
    void AddIcon(Canvas c, Element e, Color col) {
      double W = c.Width, H = c.Height;
      double cx = (e.U + 1) / 2 * W, cy = (1 - e.V) / 2 * H;
      double pw = e.HalfW * W, ph = e.HalfH * H;
      var r = new Rectangle {
        Width = pw, Height = ph,
        Fill = new SolidColorBrush(col),
        OpacityMask = new ImageBrush(IconImage((IconId)e.Ref)),
      };
      Canvas.SetLeft(r, cx - pw / 2);
      Canvas.SetTop(r, cy - ph / 2);
      c.Children.Add(r);
    }

    // Static cockpit silhouette so the preview reads like an in-headset view.
    // Drawn behind the overlay content, with a small per-eye horizontal parallax
    // (dx) so the two eyes sit offset like a real stereo pair. Preview only: not
    // part of the shared-memory contract or the layer render.
    void DrawCockpit(Canvas c, bool leftEye) {
      double dx = leftEye ? kEyeParallax : -kEyeParallax;

      // Prefer the cockpit PNG embedded in the plugin DLL; fall back to the
      // vector silhouette when none is bundled. The PNG takes the same per-eye
      // horizontal parallax as the vector art.
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

    // Crops an eye to the selected headset's visible area (estimated per edge; see
    // HeadsetMasks): the view shrinks to that area and the canvas shifts so the
    // area sits at its origin. The Viewbox then scales the crop up to fill the tab.
    void CropToHeadset(Grid view, Canvas c, bool leftEye) {
      var (x, y, w, h) = HeadsetMasks.VisiblePixels(_s.Headset, leftEye, c.Width);
      view.Width = w;
      view.Height = h;
      c.Margin = new Thickness(-x, -y, 0, 0);
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

    // Matches the layer's glow: the element colour at the centre fading to transparent.
    static void AddGlow(Canvas c, double u, double v, double hw, double hh, Color col) {
      double W = c.Width, H = c.Height;
      double cx = (u + 1) / 2 * W, cy = (1 - v) / 2 * H;
      double pw = hw * W, ph = hh * H;
      var fill = new RadialGradientBrush(col, Color.FromArgb(0, col.R, col.G, col.B));
      var e = new Ellipse { Width = pw, Height = ph, Fill = fill };
      Canvas.SetLeft(e, cx - pw / 2);
      Canvas.SetTop(e, cy - ph / 2);
      c.Children.Add(e);
    }

    // Isoceles triangle, apex pointing "up" in NDC then rotated by `angle` radians
    // (clockwise from up, matching a bearing measured as atan2(x,-y)).
    static void AddTriangle(Canvas c, double u, double v, double hw, double hh, double angle, Color col) {
      double W = c.Width, H = c.Height;
      double cx = (u + 1) / 2 * W, cy = (1 - v) / 2 * H;
      double pw = hw * W / 2, ph = hh * H / 2;   // half-extents in pixels
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
  }
}
