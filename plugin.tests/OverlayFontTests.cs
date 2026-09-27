using NUnit.Framework;
using OpenXRSimHubAlerts.Plugin;

public class OverlayFontTests {
  [Test] public void LoadsTheBundledInterBold() =>
    Assert.That(OverlayFont.Face.FamilyNames.Values, Has.Some.Contains("Inter"));

  [Test] public void DigitsShareOneAdvance() {
    double zero = OverlayFont.Advance('0');
    Assert.That(zero, Is.GreaterThan(0.4).And.LessThan(0.8));
    foreach (char c in "123456789") Assert.That(OverlayFont.Advance(c), Is.EqualTo(zero).Within(0.02), c.ToString());
  }

  [Test] public void UnknownCharactersHaveNoAdvance() =>
    Assert.That(OverlayFont.Advance('一'), Is.EqualTo(0));
}
