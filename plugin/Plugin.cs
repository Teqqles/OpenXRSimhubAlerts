// plugin/Plugin.cs
using System;
using System.Collections.Generic;
using System.IO;
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
    readonly System.Diagnostics.Stopwatch _clock = System.Diagnostics.Stopwatch.StartNew();
    readonly ShiftLights _shift = new ShiftLights();
    readonly DriverAids _aids = new DriverAids();
    SettingsAutosave _autosave;
    // SimHub configures log4net, so this writes to SimHub's log.
    static readonly log4net.ILog Log = log4net.LogManager.GetLogger(typeof(Plugin));

    public ImageSource PictureIcon => null;
    public string LeftMenuTitle => "OpenXR SimHub Alerts";

    public void Init(PluginManager pm) {
      PluginManager = pm;
      // Own file: SimHub named the old one after our class, "Plugin", which other plugins share.
      string settingsPath = Path.GetFullPath(pm.GetCommonStoragePath("OpenXRSimHubAlerts", SettingsStore.FileName));
      string legacyPath = Path.GetFullPath(pm.GetCommonStoragePath("Plugin.General.json"));
      Settings = SettingsStore.Load(settingsPath, legacyPath, out string note);
      Log.Info("OpenXR SimHub Alerts: " + note);
      _autosave = new SettingsAutosave(Settings, settingsPath);
      _autosave.SaveFailed += error => Log.Warn("OpenXR SimHub Alerts: could not save settings to " + settingsPath + ": " + error);
      _autosave.SaveRecovered += () => Log.Info("OpenXR SimHub Alerts: settings saved again after an earlier failure");
      _writer = new SharedMemoryWriter();
      _block = new DataBlock { Elements = new Element[ShmContract.MaxElements] };
    }

    public void DataUpdate(PluginManager pm, ref GameData data) {
      _autosave.Tick(_clock.Elapsed.TotalSeconds);
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
      ShiftState shift = _shift.Update(ReadShift(g), _clock.Elapsed.TotalSeconds,
                                       ShiftLights.LightCount(Settings.ShiftLightCount));
      DriverAidState aids = _aids.Update(ReadAids(g), _clock.Elapsed.TotalSeconds);
      OverlayComposer.Compose(Settings, flags, _cars, carCount, shift, aids, ref _block);
      _writer.Write(ref _block);
    }

    // Demo mode: publish synthetic cycling flags, orbiting radar blips and an RPM
    // sweep so the overlay can be previewed in-headset (in any OpenXR title)
    // without a sim.
    void WriteDemo() {
      _block.Connected = 1;
      double t = _clock.Elapsed.TotalSeconds;
      uint carCount = DemoData.Fill(t, _cars, out byte flags);
      ShiftState shift = _shift.Update(DemoData.Shift(t), t, ShiftLights.LightCount(Settings.ShiftLightCount));
      DriverAidState aids = _aids.Update(DemoData.Aids(t), t);
      OverlayComposer.Compose(Settings, flags, _cars, carCount, shift, aids, ref _block);
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

    // StartRpm is left at 0 so the row starts at 75% of MaxRpm: SimHub's
    // CarSettings_MinimumShownRPM is obsolete with no replacement, and
    // CarSettings_RPMShiftLight1/2 may be fractions rather than RPM.
    static ShiftInput ReadShift(StatusDataBase g) {
      try {
        return new ShiftInput {
          Rpm        = g.Rpms,
          MaxRpm     = g.MaxRpm > 0 ? g.MaxRpm : g.CarSettings_MaxRPM,
          RedlineRpm = g.CarSettings_CurrentGearRedLineRPM > 0
                         ? g.CarSettings_CurrentGearRedLineRPM : g.CarSettings_RedLineRPM,
          Gear       = g.Gear,
        };
      } catch {
        return default;
      }
    }

    static DriverAidInput ReadAids(StatusDataBase g) {
      try {
        return new DriverAidInput {
          Abs = g.ABSActive != 0,
          Tc = g.TCActive != 0,
          DrsAvailable = g.DRSAvailable != 0,
          DrsOpen = g.DRSEnabled != 0,
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
      _autosave?.Flush();
      _writer?.Dispose();
    }

    public System.Windows.Controls.Control GetWPFSettingsControl(PluginManager pm)
      => new ui.SettingsControl(Settings, _autosave);
  }
}
