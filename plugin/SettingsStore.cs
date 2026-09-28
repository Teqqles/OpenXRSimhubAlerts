// plugin/SettingsStore.cs
using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Text;
using System.Web.Script.Serialization;

namespace OpenXRSimHubAlerts.Plugin {
  // Every public field of Settings is a setting, so new ones need no change here.
  // Files overlay the defaults, so older files keep working.
  public static class SettingsStore {
    public const string FileName = "settings.json";

    static readonly FieldInfo[] Fields = typeof(Settings).GetFields(BindingFlags.Public | BindingFlags.Instance);
    static readonly JavaScriptSerializer Json = new JavaScriptSerializer();

    // One setting per line, so exports read and diff well.
    public static string Serialize(Settings s) {
      var sb = new StringBuilder("{\n");
      for (int i = 0; i < Fields.Length; i++) {
        sb.Append("  ").Append(Json.Serialize(Fields[i].Name)).Append(": ")
          .Append(Json.Serialize(Fields[i].GetValue(s)))
          .Append(i + 1 < Fields.Length ? ",\n" : "\n");
      }
      return sb.Append("}\n").ToString();
    }

    // Wrong shape or type fails with a reason. Unknown names are ignored; missing,
    // null, NaN and undefined enum values keep defaults; numbers clamp to [Limits].
    public static bool TryDeserialize(string json, out Settings settings, out string error) {
      settings = null;
      error = null;
      Dictionary<string, object> values;
      try {
        values = Json.DeserializeObject(json ?? "") as Dictionary<string, object>;
      } catch (ArgumentException ex) {   // malformed JSON
        error = ex.Message;
        return false;
      }
      if (values == null) {
        error = "the file is not a settings object";
        return false;
      }
      var s = new Settings();
      foreach (var f in Fields) {
        if (!values.TryGetValue(f.Name, out object value) || value == null) continue;
        try {
          f.SetValue(s, WithinLimits(f, Json.ConvertToType(value, f.FieldType), f.GetValue(s)));
        } catch (Exception ex) {   // the number converters throw a bare Exception
          error = f.Name + ": " + ex.Message;
          return false;
        }
      }
      settings = s;
      return true;
    }

    static object WithinLimits(FieldInfo f, object value, object fallback) {
      if (f.FieldType.IsEnum) return Enum.IsDefined(f.FieldType, value) ? value : fallback;
      var limits = f.GetCustomAttribute<LimitsAttribute>();
      if (limits == null) return value;
      double number = Convert.ToDouble(value);
      if (double.IsNaN(number)) return fallback;
      return Convert.ChangeType(Math.Max(limits.Min, Math.Min(limits.Max, number)), f.FieldType);
    }

    // In place, because the plugin and the UI share one Settings instance.
    public static void CopyInto(Settings from, Settings to) {
      foreach (var f in Fields) f.SetValue(to, f.GetValue(from));
    }

    // Temp file swapped in, so a crash mid-write keeps the old file (as .bak).
    public static void Save(string path, Settings s) => SaveSerialized(path, Serialize(s));

    public static void SaveSerialized(string path, string json) {
      Directory.CreateDirectory(Path.GetDirectoryName(path));
      string tmp = path + ".tmp";
      File.WriteAllText(tmp, json, new UTF8Encoding(false));
      if (File.Exists(path)) File.Replace(tmp, path, path + ".bak");
      else File.Move(tmp, path);
    }

    // Never throws. Tries our file, its .bak, the legacy file, then defaults.
    // `note` says which, for the log.
    public static Settings Load(string path, string legacyPath, out string note) {
      try {
        if (File.Exists(path)) return LoadOwnFile(path, out note);
        if (legacyPath != null && File.Exists(legacyPath)) return MigrateLegacy(legacyPath, path, out note);
        note = "no saved settings yet; using defaults";
        return new Settings();
      } catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException) {
        note = "could not read or write settings (" + ex.Message + "); using defaults";
        return new Settings();
      }
    }

    static Settings LoadOwnFile(string path, out string note) {
      if (TryRead(path, out var loaded, out string error)) {
        note = "loaded " + path;
        return loaded;
      }
      string corrupt = path + ".corrupt";
      string backup = path + ".bak";
      File.Copy(path, corrupt, true);
      if (File.Exists(backup) && TryRead(backup, out var restored, out _)) {
        // Restore now, so the next start finds it even if nothing changes.
        File.Copy(backup, path, true);
        note = path + " was unreadable (" + error + "), kept as " + corrupt + "; restored the backup";
        return restored;
      }
      File.Delete(path);
      note = path + " was unreadable (" + error + "), kept as " + corrupt + ", and there is no usable backup; using defaults";
      return new Settings();
    }

    static Settings MigrateLegacy(string legacyPath, string path, out string note) {
      string json = File.ReadAllText(legacyPath);
      if (!LooksLikeOurs(json)) {
        note = legacyPath + " holds another plugin's settings; using defaults";
        return new Settings();
      }
      if (!TryDeserialize(json, out var migrated, out string error)) {
        note = "could not migrate " + legacyPath + " (" + error + "); using defaults";
        return new Settings();
      }
      try {
        Save(path, migrated);
      } catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException) {
        note = "loaded settings from " + legacyPath + " but could not write " + path
             + " (" + ex.Message + "); the next change or start tries again";
        return migrated;
      }
      note = "migrated settings from " + legacyPath + " to " + path;
      return migrated;
    }

    static bool TryRead(string path, out Settings s, out string error) =>
      TryDeserialize(File.ReadAllText(path), out s, out error);

    // Other plugins' classes may also be named "Plugin", so check the file is ours.
    static bool LooksLikeOurs(string json) {
      try {
        return Json.DeserializeObject(json) is Dictionary<string, object> values
            && values.ContainsKey(nameof(Settings.EnableFlags))
            && values.ContainsKey(nameof(Settings.RadarRange))
            && values.ContainsKey(nameof(Settings.PosFlagx));
      } catch (ArgumentException) {
        return false;
      }
    }
  }

  // Saves soon after a change, so a crash loses little. Compares serialized
  // settings, so the UI need not report changes.
  public sealed class SettingsAutosave {
    public const double IntervalSeconds = 2.0;

    readonly Settings _settings;
    readonly string _path;
    // Autosave (data thread) and import (UI thread) take turns, so no save sees half an import.
    readonly object _sync = new object();
    string _saved;
    string _lastError;
    double _due = IntervalSeconds;

    public SettingsAutosave(Settings settings, string path) {
      _settings = settings;
      _path = path;
      _saved = SettingsStore.Serialize(settings);
    }

    // Once per distinct failure, so a stuck save does not flood the log.
    public event Action<string> SaveFailed;
    // First successful save after a failure.
    public event Action SaveRecovered;

    // Call every data update; saves at most once per interval.
    public bool Tick(double timeSeconds) {
      if (timeSeconds < _due) return false;
      _due = timeSeconds + IntervalSeconds;
      return Flush();
    }

    // Imports and saves at once.
    public void Replace(Settings imported) {
      lock (_sync) {
        SettingsStore.CopyInto(imported, _settings);
        SaveIfChanged();
      }
    }

    // Saves if changed; the next call retries a failure.
    public bool Flush() {
      lock (_sync) return SaveIfChanged();
    }

    bool SaveIfChanged() {
      string json = SettingsStore.Serialize(_settings);
      if (json == _saved) return false;
      try {
        SettingsStore.SaveSerialized(_path, json);
      } catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException) {
        if (ex.Message != _lastError) SaveFailed?.Invoke(ex.Message);
        _lastError = ex.Message;
        return false;
      }
      _saved = json;
      if (_lastError != null) SaveRecovered?.Invoke();
      _lastError = null;
      return true;
    }
  }
}
