// plugin/OverlayFont.cs
using System;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Windows.Media;

namespace OpenXRSimHubAlerts.Plugin {
  // The overlay font (Inter Bold), shared with the layer's atlas so text laid out
  // here lands where the layer draws it. GlyphTypeface needs a file, so the
  // embedded font is written once to the temp folder.
  public static class OverlayFont {
    const string Resource = "OpenXRSimHubAlerts.Fonts.Inter-Bold.ttf";
    static readonly Lazy<GlyphTypeface> _face = new Lazy<GlyphTypeface>(TryLoad);
    static readonly Lazy<double> _digitAdvance = new Lazy<double>(() => Enumerable.Range('0', 10).Max(c => Advance((char)c)));

    // Null when the embedded font could not be loaded (missing resource, an
    // unwritable temp folder, a corrupt font). Callers must treat that as "no
    // font" and skip text rather than dereference it.
    public static GlyphTypeface Face => _face.Value;

    public static bool Available => Face != null;

    // Pen advance in em; 0 for characters the font lacks, or when the font is
    // unavailable.
    public static double Advance(char c) {
      GlyphTypeface face = Face;
      return face != null && face.CharacterToGlyphMap.TryGetValue(c, out ushort glyph)
        ? face.AdvanceWidths[glyph] : 0;
    }

    // Width of the widest digit, in em; 0 when the font is unavailable. Inter's
    // digits are proportional ('1' is narrow); TextLayout gives every digit this
    // width so readouts do not shift as their digits change.
    public static double DigitAdvance => _digitAdvance.Value;

    // Lazy<T> caches the first result forever, including an exception, so a
    // load failure must never throw out of here: it is caught and turned into
    // a null Face that every caller already has to handle.
    static GlyphTypeface TryLoad() {
      try {
        using (var s = typeof(OverlayFont).Assembly.GetManifestResourceStream(Resource)) {
          if (s == null) return null;
          byte[] bytes;
          using (var mem = new MemoryStream()) {
            s.CopyTo(mem);
            bytes = mem.ToArray();
          }
          string path = TempPathFor(bytes);
          if (!File.Exists(path) || new FileInfo(path).Length != bytes.Length)
            File.WriteAllBytes(path, bytes);
          return new GlyphTypeface(new Uri(path));
        }
      } catch (IOException) {
        return null;
      } catch (UnauthorizedAccessException) {
        return null;
      } catch (FileFormatException) {
        return null;
      } catch (NotSupportedException) {
        return null;
      } catch (ArgumentException) {
        return null;
      }
    }

    // The temp file name for the given font bytes, keyed on their content (the
    // first 16 hex characters of a SHA-256 hash) rather than their length, so a
    // stale or partially written file of the same length is never mistaken for
    // a match. Public (though not part of the font-loading API) so tests can
    // check the hashing without touching a real GlyphTypeface.
    public static string TempPathFor(byte[] bytes) {
      string hash;
      using (var sha = SHA256.Create()) {
        byte[] digest = sha.ComputeHash(bytes);
        hash = BitConverter.ToString(digest, 0, 8).Replace("-", "").ToLowerInvariant();
      }
      return Path.Combine(Path.GetTempPath(), $"OpenXRSimHubAlerts-Inter-Bold-{hash}.ttf");
    }
  }
}
