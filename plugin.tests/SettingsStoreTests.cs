// plugin.tests/SettingsStoreTests.cs
using System;
using System.IO;
using System.Reflection;
using NUnit.Framework;
using OpenXRSimHubAlerts.Plugin;
using OpenXRSimHubAlerts.Shared;

public class SettingsStoreTests {
  string _dir;
  string Main => Path.Combine(_dir, "settings.json");
  string Legacy => Path.Combine(_dir, "Plugin.General.json");

  [SetUp] public void CreateDir() {
    _dir = Path.Combine(Path.GetTempPath(), "oxr-settings-" + Guid.NewGuid().ToString("N"));
    Directory.CreateDirectory(_dir);
  }

  [TearDown] public void DeleteDir() {
    if (Directory.Exists(_dir)) Directory.Delete(_dir, true);
  }

  static Settings Changed() => new Settings {
    Shape = 4, RefreshMode = RefreshMode.Fps15, DemoMode = true, Headset = "Meta Quest 3",
    PosFlagx = -0.25f, RadarRange = 58.5f, ShiftLightCount = 14, EnableDrs = false, PosAidsy = -0.4f,
  };

  static void AssertSame(Settings expected, Settings actual) {
    foreach (var f in typeof(Settings).GetFields(BindingFlags.Public | BindingFlags.Instance))
      Assert.That(f.GetValue(actual), Is.EqualTo(f.GetValue(expected)), f.Name);
  }

  [Test] public void RoundTripKeepsEveryField() {
    Assert.That(SettingsStore.TryDeserialize(SettingsStore.Serialize(Changed()), out var back, out _), Is.True);
    AssertSame(Changed(), back);
  }

  [Test] public void MissingFieldsKeepTheirDefaults() {
    Assert.That(SettingsStore.TryDeserialize("{\"Shape\":3}", out var s, out _), Is.True);
    Assert.That(s.Shape, Is.EqualTo(3));
    Assert.That(s.PosAidsy, Is.EqualTo(new Settings().PosAidsy));
    Assert.That(s.EnableShiftLights, Is.True);
  }

  [Test] public void UnknownFieldsAreIgnored() {
    Assert.That(SettingsStore.TryDeserialize("{\"Shape\":2,\"NoSuchSetting\":7}", out var s, out _), Is.True);
    Assert.That(s.Shape, Is.EqualTo(2));
  }

  [TestCase("")]
  [TestCase("not json")]
  [TestCase("[1,2]")]
  [TestCase("{\"Shape\":\"square\"}")]
  public void InvalidJsonIsRejectedWithAReason(string json) {
    Assert.That(SettingsStore.TryDeserialize(json, out var s, out var error), Is.False);
    Assert.That(s, Is.Null);
    Assert.That(error, Is.Not.Empty);
  }

  [Test] public void CopyIntoCopiesEveryFieldIntoTheSameInstance() {
    var target = new Settings();
    SettingsStore.CopyInto(Changed(), target);
    AssertSame(Changed(), target);
  }

  [Test] public void SaveThenLoadRoundTrips() {
    SettingsStore.Save(Main, Changed());
    AssertSame(Changed(), SettingsStore.Load(Main, Legacy, out _));
  }

  [Test] public void SaveCreatesTheFolder() {
    string nested = Path.Combine(_dir, "a", "b", "settings.json");
    SettingsStore.Save(nested, Changed());
    Assert.That(File.Exists(nested), Is.True);
  }

  [Test] public void ASecondSaveKeepsTheFirstAsBackup() {
    SettingsStore.Save(Main, new Settings { Shape = 1 });
    SettingsStore.Save(Main, new Settings { Shape = 2 });
    Assert.That(SettingsStore.TryDeserialize(File.ReadAllText(Main + ".bak"), out var bak, out _), Is.True);
    Assert.That(bak.Shape, Is.EqualTo(1));
  }

  [Test] public void ACorruptFileFallsBackToTheBackupAndIsSetAside() {
    SettingsStore.Save(Main, new Settings { Shape = 1 });
    SettingsStore.Save(Main, new Settings { Shape = 2 });
    File.WriteAllText(Main, "{ broken");
    var s = SettingsStore.Load(Main, Legacy, out string note);
    Assert.That(s.Shape, Is.EqualTo(1));
    Assert.That(note, Does.Contain("backup"));
    Assert.That(File.Exists(Main + ".corrupt"), Is.True);
    Assert.That(File.Exists(Main), Is.False, "the next save starts clean and keeps the good backup");
  }

  [Test] public void ACorruptFileWithNoBackupGivesDefaults() {
    File.WriteAllText(Main, "{ broken");
    var s = SettingsStore.Load(Main, Legacy, out string note);
    AssertSame(new Settings(), s);
    Assert.That(note, Does.Contain("default"));
  }

  [Test] public void OurLegacyFileIsMigratedOnce() {
    File.WriteAllText(Legacy, SettingsStore.Serialize(Changed()));
    var s = SettingsStore.Load(Main, Legacy, out string note);
    AssertSame(Changed(), s);
    Assert.That(note, Does.Contain("migrated"));
    Assert.That(File.Exists(Main), Is.True);
    Assert.That(File.Exists(Legacy), Is.True, "the old file is left in place");
  }

  [Test] public void ALegacyFileFromAnotherPluginIsIgnored() {
    File.WriteAllText(Legacy, "{\"SomeOtherPluginSetting\":true,\"Port\":\"COM3\"}");
    var s = SettingsStore.Load(Main, Legacy, out _);
    AssertSame(new Settings(), s);
    Assert.That(File.Exists(Main), Is.False);
  }

  [Test] public void OurFileWinsOverTheLegacyFile() {
    SettingsStore.Save(Main, new Settings { Shape = 1 });
    File.WriteAllText(Legacy, SettingsStore.Serialize(new Settings { Shape = 3 }));
    Assert.That(SettingsStore.Load(Main, Legacy, out _).Shape, Is.EqualTo(1));
  }

  [Test] public void NothingOnDiskGivesDefaults() =>
    AssertSame(new Settings(), SettingsStore.Load(Main, Legacy, out _));

  [Test] public void AutosaveWritesOnlyWhenSomethingChanged() {
    var s = new Settings();
    var autosave = new SettingsAutosave(s, Main);
    Assert.That(autosave.Tick(0), Is.False, "unchanged since start");
    s.Shape = 2;
    Assert.That(autosave.Tick(1), Is.False, "not due yet");
    Assert.That(autosave.Tick(SettingsAutosave.IntervalSeconds), Is.True);
    Assert.That(SettingsStore.Load(Main, Legacy, out _).Shape, Is.EqualTo(2));
    Assert.That(autosave.Tick(SettingsAutosave.IntervalSeconds * 2), Is.False, "already saved");
  }

  [Test] public void FlushSavesAPendingChangeImmediately() {
    var s = new Settings();
    var autosave = new SettingsAutosave(s, Main);
    s.EnableRadar = false;
    Assert.That(autosave.Flush(), Is.True);
    Assert.That(SettingsStore.Load(Main, Legacy, out _).EnableRadar, Is.False);
    Assert.That(autosave.Flush(), Is.False);
  }

  [Test] public void AFailedAutosaveIsRetriedNextTime() {
    var s = new Settings();
    string blocked = Path.Combine(_dir, "blocked");
    File.WriteAllText(blocked, "a file where the folder should be");
    var autosave = new SettingsAutosave(s, Path.Combine(blocked, "settings.json"));
    s.Shape = 2;
    Assert.That(autosave.Flush(), Is.False);
    File.Delete(blocked);
    Assert.That(autosave.Flush(), Is.True);
  }
}
