// plugin.tests/SettingsTests.cs
using NUnit.Framework;
using OpenXRSimHubAlerts.Plugin;
using OpenXRSimHubAlerts.Shared;

public class SettingsTests {
  [Test] public void RefreshModeDefaultsToAuto() =>
    Assert.That(new Settings().RefreshMode, Is.EqualTo(RefreshMode.Auto));
}
