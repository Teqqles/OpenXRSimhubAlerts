// plugin/ui/SettingsControl.xaml.cs
using System;
using System.Windows.Controls;

namespace OpenXRSimHubAlerts.Plugin.ui {
  public partial class SettingsControl : UserControl {
    readonly Settings _s;

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
    }

    void UpdateValueLabels() {
      RadarRangeValue.Text = RadarRange.Value.ToString("F0");
      ScaleFlagValue.Text = ScaleFlag.Value.ToString("F1");
      PosFlagXValue.Text = PosFlagX.Value.ToString("F2");
      PosFlagYValue.Text = PosFlagY.Value.ToString("F2");
    }
  }
}
