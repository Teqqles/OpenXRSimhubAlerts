// plugin/SettingsStore.cs
using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Text;
using System.Web.Script.Serialization;

namespace OpenXRSimHubAlerts.Plugin {
  // Reads and writes the plugin's own settings file (and export files) as JSON.
  // Every public field of Settings is a setting, so new settings are saved, loaded,
  // exported and imported without touching this class. Loading starts from the
  // defaults and overlays whatever the file holds, so older files keep working.
  public static class SettingsStore {
    public const string FileName = "settings.json";

    static readonly FieldInfo[] Fields = typeof(Settings).GetFields(BindingFlags.Public | BindingFlags.Instance);
    static readonly JavaScriptSerializer Json = new JavaScriptSerializer();

    // One setting per line, so export files are easy to read and diff.
    public static string Serialize(Settings s) {
      var sb = new StringBuilder("{\n");
      for (int i = 0; i < Fields.Length; i++) {
        sb.Append("  ").Append(Json.Serialize(Fields[i].Name)).Append(": ")
          .Append(Json.Serialize(Fields[i].GetValue(s)))
          .Append(i + 1 < Fields.Length ? ",\n" : "\n");
      }
      return sb.Append("}\n").ToString();
    }

    // False with a reason for anything that is not a settings object or holds a
    // value of the wrong type. Unknown names are ignored; missing ones keep defaults.
    public static bool TryDeserialize(string json, out Settings settings, out string error) {
      settings = null;
      error = null;
      try {
        if (!(Json.DeserializeObject(json ?? "") is Dictionary<string, object> values)) {
          error = "the file is not a settings object";
          return false;
        }
        var s = new Settings();
        foreach (var f in Fields)
          if (values.TryGetValue(f.Name, out object value))
            f.SetValue(s, Json.ConvertToType(value, f.FieldType));
        settings = s;
        return true;
      } catch (Exception ex) {   // any parse or conversion failure means "not a valid file"
        error = ex.Message;
        return false;
      }
    }

    // Copies every setting into an existing instance, so the plugin and the
    // settings UI, which share one Settings object, both see an import at once.
    public static void CopyInto(Settings from, Settings to) {
      foreach (var f in Fields) f.SetValue(to, f.GetValue(from));
    }

    // Writes to a temp file, then swaps it in; the previous file becomes .bak.
    public static void Save(string path, Settings s) {
      Directory.CreateDirectory(Path.GetDirectoryName(path));
      string tmp = path + ".tmp";
      File.WriteAllText(tmp, Serialize(s), new UTF8Encoding(false));
      if (File.Exists(path)) File.Replace(tmp, path, path + ".bak");
      else File.Move(tmp, path);
    }

    // Never throws. Order: our file; if that is unreadable, set it aside as
    // .corrupt and use .bak; else migrate the legacy SimHub common-settings file
    // when it is ours; else defaults. `note` says which, for the log.
    public static Settings Load(string path, string legacyPath, out string note) {
      try {
        if (File.Exists(path)) {
          if (TryRead(path, out var s)) { note = "loaded " + path; return s; }
          File.Copy(path, path + ".corrupt", true);
          File.Delete(path);
          if (TryRead(path + ".bak", out var bak)) {
            note = path + " was unreadable (kept as .corrupt); loaded the backup";
            return bak;
          }
          note = path + " was unreadable (kept as .corrupt) and there is no usable backup; using defaults";
          return new Settings();
        }
        if (legacyPath != null && File.Exists(legacyPath)) {
          string json = File.ReadAllText(legacyPath);
          if (LooksLikeOurs(json) && TryDeserialize(json, out var old, out _)) {
            Save(path, old);
            note = "migrated settings from " + legacyPath + " to " + path;
            return old;
          }
        }
        note = "no saved settings yet; using defaults";
        return new Settings();
      } catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException) {
        note = "could not read or write settings (" + ex.Message + "); using defaults";
        return new Settings();
      }
    }

    static bool TryRead(string path, out Settings s) {
      s = null;
      return File.Exists(path) && TryDeserialize(File.ReadAllText(path), out s, out _);
    }

    // SimHub names its common-settings file after the plugin class ("Plugin"), a
    // name other plugins may share, so only migrate a file with our own settings.
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

  // Saves the settings shortly after they change instead of only when SimHub
  // closes. Compares the serialized settings every IntervalSeconds, so every
  // setting is covered without the UI having to announce changes.
  public sealed class SettingsAutosave {
    public const double IntervalSeconds = 2.0;

    readonly Settings _settings;
    readonly string _path;
    string _saved;
    double _due = IntervalSeconds;

    public SettingsAutosave(Settings settings, string path) {
      _settings = settings;
      _path = path;
      _saved = SettingsStore.Serialize(settings);
    }

    // Why the last save failed, or null.
    public string LastError { get; private set; }

    // Call often (every data update); saves at most once per interval.
    public bool Tick(double timeSeconds) {
      if (timeSeconds < _due) return false;
      _due = timeSeconds + IntervalSeconds;
      return Flush();
    }

    // Saves now if anything changed. A failed save is retried by the next call.
    public bool Flush() {
      string json = SettingsStore.Serialize(_settings);
      if (json == _saved) return false;
      try {
        SettingsStore.Save(_path, _settings);
        _saved = json;
        LastError = null;
        return true;
      } catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException) {
        LastError = ex.Message;
        return false;
      }
    }
  }
}
