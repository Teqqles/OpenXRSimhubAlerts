// plugin/Plugin.cs
using System;
using System.Collections.Generic;
using System.Windows.Media;
using GameReaderCommon;
using SimHub.Plugins;
using OpenXRSimHubAlerts.Shared;

namespace OpenXRSimHubAlerts.Plugin {
  [PluginName("OpenXR SimHub Alerts")]
  [PluginDescription("Peripheral flag + radar overlay for OpenXR VR")]
  [PluginAuthor("OpenXRSimHubAlerts")]
  public class Plugin : IPlugin, IDataPlugin, IWPFSettingsV2 {
    public PluginManager PluginManager { get; set; }
    public Settings Settings;
    SharedMemoryWriter _writer;
    DataBlock _block;
    readonly CarBlip[] _cars = new CarBlip[RadarCalculator.MaxCars];
    readonly List<Opponent> _opps = new List<Opponent>();
    readonly System.Diagnostics.Stopwatch _demoClock = System.Diagnostics.Stopwatch.StartNew();

    public ImageSource PictureIcon => null;
    public string LeftMenuTitle => "OpenXR SimHub Alerts";

    public void Init(PluginManager pm) {
      PluginManager = pm;
      Settings = this.ReadCommonSettings("General", () => new Settings());
      _writer = new SharedMemoryWriter();
      _block = new DataBlock { Elements = new Element[ShmContract.MaxElements] };
    }

    public void DataUpdate(PluginManager pm, ref GameData data) {
      if (Settings.DemoMode) { WriteDemo(); return; }

      var g = data.NewData;
      if (g == null) {
        _block.Connected = 0;
        _writer.Write(ref _block);
        return;
      }

      _block.Connected = 1;
      byte flags = FlagMapper.Map(ReadFlags(g));

      _opps.Clear();
      var opponents = g.OpponentsAheadOnTrack ?? new List<GameReaderCommon.Opponent>();
      var behind = g.OpponentsBehindOnTrack ?? new List<GameReaderCommon.Opponent>();

      foreach (var op in opponents)
        _opps.Add(ToOpponent(op, g));
      foreach (var op in behind)
        _opps.Add(ToOpponent(op, g));

      uint carCount = (uint)RadarCalculator.Build(_opps, Settings.RadarRange, _cars);
      OverlayComposer.Compose(Settings, flags, _cars, carCount, default(ShiftState), ref _block);
      _writer.Write(ref _block);
    }

    // Demo mode: publish synthetic cycling flags + orbiting radar blips so the
    // overlay can be previewed in-headset (in any OpenXR title) without a sim.
    void WriteDemo() {
      _block.Connected = 1;
      double t = _demoClock.Elapsed.TotalSeconds;
      uint carCount = DemoData.Fill(t, _cars, out byte flags);
      OverlayComposer.Compose(Settings, flags, _cars, carCount, default(ShiftState), ref _block);
      _writer.Write(ref _block);
    }

    static FlagInput ReadFlags(StatusDataBase g) {
      try {
        return new FlagInput {
          Green    = g.Flag_Green > 0,
          Yellow   = g.Flag_Yellow > 0,
          Blue     = g.Flag_Blue > 0,
          White    = g.Flag_White > 0,
          Red      = false,  // SimHub has no Flag_Red
          Black    = g.Flag_Black > 0,
          Meatball = g.Flag_Orange > 0,   // orange is meatball/damage flag
        };
      } catch {
        return default;
      }
    }

    static Opponent ToOpponent(GameReaderCommon.Opponent op, StatusDataBase g) {
      try {
        double playerPos = g.TrackPositionPercent;
        double opponentPos = 0.0;

        // Try to get opponent track position via reflection (game-dependent field)
        try {
          var posField = op.GetType().GetProperty("TrackPositionPercent");
          if (posField != null) {
            var val = posField.GetValue(op);
            if (val != null)
              opponentPos = (double)val;
          }
        } catch { }

        float gap = SplineGap.Compute(opponentPos, playerPos, g.TrackLength);

        return new Opponent {
          HasRelative = false,
          SplineGap = gap,
          TrackWidthEstimate = 12f
        };
      } catch {
        return new Opponent {
          HasRelative = false,
          SplineGap = 0f,
          TrackWidthEstimate = 12f
        };
      }
    }

    public void End(PluginManager pm) {
      this.SaveCommonSettings("General", Settings);
      _writer?.Dispose();
    }

    public System.Windows.Controls.Control GetWPFSettingsControl(PluginManager pm)
      => new ui.SettingsControl(Settings);
  }
}
