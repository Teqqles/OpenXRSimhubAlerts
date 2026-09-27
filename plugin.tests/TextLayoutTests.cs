using System.Linq;
using NUnit.Framework;
using OpenXRSimHubAlerts.Plugin;
using OpenXRSimHubAlerts.Shared;

public class TextLayoutTests {
  static Element[] Lay(string text, TextAlign align = TextAlign.Left) =>
    TextLayout.Glyphs(text, 0.1f, -0.2f, 0.3f, align, 0xFFFFFFFFu, 7, Eyes.Both, ElementFlags.TimeCritical).ToArray();

  [Test] public void OneTextElementPerVisibleCharacter() {
    var e = Lay("DRS 2");
    Assert.That(e.Select(x => (char)x.Ref), Is.EqualTo("DRS2".ToCharArray()));
    Assert.That(e.All(x => x.Kind == ElementKind.Text && x.Eyes == Eyes.Both && x.Priority == 7
                          && x.Flags == ElementFlags.TimeCritical && x.Color == 0xFFFFFFFFu));
  }

  [Test] public void GlyphsSitOnTheBaselineAtTheFontSize() {
    var e = Lay("AB");
    Assert.That(e.All(x => x.V == -0.2f && x.HalfH == 0.3f));
  }

  [Test] public void PenAdvancesByEachGlyphIncludingSpaces() {
    var e = Lay("A B");
    float a = (float)(OverlayFont.Advance('A') * 0.3), space = (float)(OverlayFont.Advance(' ') * 0.3);
    Assert.That(e[0].U, Is.EqualTo(0.1f).Within(1e-6));
    Assert.That(e[1].U, Is.EqualTo(0.1f + a + space).Within(1e-6));
    Assert.That(e[0].HalfW, Is.EqualTo(a / 2).Within(1e-6));
  }

  [Test] public void CentreAndRightAlignOnThePenPosition() {
    float width = (float)(TextLayout.Width("88") * 0.3);
    Assert.That(Lay("88", TextAlign.Centre)[0].U, Is.EqualTo(0.1f - width / 2).Within(1e-6));
    Assert.That(Lay("88", TextAlign.Right)[0].U, Is.EqualTo(0.1f - width).Within(1e-6));
  }

  [Test] public void CharactersOutsidePrintableAsciiAreSkipped() =>
    Assert.That(Lay("Aé一B").Select(x => (char)x.Ref), Is.EqualTo(new[] { 'A', 'B' }));
}
