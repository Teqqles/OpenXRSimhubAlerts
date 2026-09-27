using System.Linq;
using NUnit.Framework;
using OpenXRSimHubAlerts.Plugin;

public class OverlayFontTests {
  [Test] public void LoadsTheBundledInterBold() =>
    Assert.That(OverlayFont.Face.FamilyNames.Values, Has.Some.Contains("Inter"));

  [Test] public void DigitAdvanceIsTheWidestDigit() {
    double widest = "0123456789".Max(c => OverlayFont.Advance(c));
    Assert.That(OverlayFont.DigitAdvance, Is.EqualTo(widest));
    Assert.That(widest, Is.GreaterThan(0.4).And.LessThan(0.8));
    Assert.That(OverlayFont.Advance('1'), Is.LessThan(widest), "Inter's digits are proportional");
  }

  [Test] public void UnknownCharactersHaveNoAdvance() =>
    Assert.That(OverlayFont.Advance('一'), Is.EqualTo(0));
}
