using System.Linq;
using NUnit.Framework;
using OpenXRSimHubAlerts.Plugin;

public class OverlayFontTests {
  [Test] public void LoadsTheBundledInterBold() =>
    Assert.That(OverlayFont.Face.FamilyNames.Values, Has.Some.Contains("Inter"));

  [Test] public void FontIsAvailable() =>
    Assert.That(OverlayFont.Available, Is.True);

  [Test] public void DigitAdvanceIsTheWidestDigit() {
    double widest = "0123456789".Max(c => OverlayFont.Advance(c));
    Assert.That(OverlayFont.DigitAdvance, Is.EqualTo(widest));
    Assert.That(widest, Is.GreaterThan(0.4).And.LessThan(0.8));
    Assert.That(OverlayFont.Advance('1'), Is.LessThan(widest), "Inter's digits are proportional");
  }

  [Test] public void UnknownCharactersHaveNoAdvance() =>
    Assert.That(OverlayFont.Advance('一'), Is.EqualTo(0));

  // The temp file OverlayFont writes the embedded font to must be keyed on its
  // content, not just its length: otherwise a stale file of the same length
  // (from an older build, or a partial write) would be reused unchanged.
  [Test] public void TempPathIsKeyedOnContentNotLength() {
    var a = new byte[] { 1, 2, 3, 4 };
    var aAgain = new byte[] { 1, 2, 3, 4 };
    var bSameLength = new byte[] { 4, 3, 2, 1 };
    Assert.That(OverlayFont.TempPathFor(a), Is.EqualTo(OverlayFont.TempPathFor(aAgain)));
    Assert.That(OverlayFont.TempPathFor(a), Is.Not.EqualTo(OverlayFont.TempPathFor(bSameLength)));
  }

  [Test] public void TempPathNameCarriesAHexHashNotTheByteLength() {
    var bytes = new byte[12345];
    string path = OverlayFont.TempPathFor(bytes);
    string name = System.IO.Path.GetFileNameWithoutExtension(path);
    string hash = name.Substring(name.LastIndexOf('-') + 1);
    Assert.That(hash, Does.Not.Contain(bytes.Length.ToString()));
    Assert.That(hash.Length, Is.EqualTo(16));
    Assert.That(hash, Does.Match("^[0-9a-f]{16}$"));
  }
}
