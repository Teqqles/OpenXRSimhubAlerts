// plugin/OverlayFont.cs
using System;
using System.IO;
using System.Windows.Media;

namespace OpenXRSimHubAlerts.Plugin {
  // The overlay font (Inter Bold), shared with the layer's atlas so text laid out
  // here lands where the layer draws it. GlyphTypeface needs a file, so the
  // embedded font is written once to the temp folder.
  public static class OverlayFont {
    const string Resource = "OpenXRSimHubAlerts.Fonts.Inter-Bold.ttf";
    static readonly Lazy<GlyphTypeface> _face = new Lazy<GlyphTypeface>(Load);

    public static GlyphTypeface Face => _face.Value;

    // Pen advance in em; 0 for characters the font lacks.
    public static double Advance(char c) =>
      Face.CharacterToGlyphMap.TryGetValue(c, out ushort glyph) ? Face.AdvanceWidths[glyph] : 0;

    static GlyphTypeface Load() {
      using (var s = typeof(OverlayFont).Assembly.GetManifestResourceStream(Resource)) {
        if (s == null) throw new InvalidOperationException("embedded font missing: " + Resource);
        string path = Path.Combine(Path.GetTempPath(), $"OpenXRSimHubAlerts-Inter-Bold-{s.Length}.ttf");
        if (!File.Exists(path) || new FileInfo(path).Length != s.Length)
          using (var f = File.Create(path)) s.CopyTo(f);
        return new GlyphTypeface(new Uri(path));
      }
    }
  }
}
