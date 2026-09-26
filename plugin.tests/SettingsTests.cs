// plugin.tests/SettingsTests.cs
using NUnit.Framework;
using OpenXRSimHubAlerts.Plugin;
using OpenXRSimHubAlerts.Shared;

public class SettingsTests {
  [Test] public void RefreshModeDefaultsToAuto() =>
    Assert.That(new Settings().RefreshMode, Is.EqualTo(RefreshMode.Auto));

  [Test] public void ToConfigCarriesRefreshMode() {
    var s = new Settings { RefreshMode = RefreshMode.Fps15 };
    Assert.That(s.ToConfig().RefreshMode, Is.EqualTo(RefreshMode.Fps15));
  }
}
