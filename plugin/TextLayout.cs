// plugin/TextLayout.cs
using System.Collections.Generic;
using OpenXRSimHubAlerts.Shared;

namespace OpenXRSimHubAlerts.Plugin {
  public enum TextAlign { Left, Centre, Right }

  // Lays a string out as one Text element per glyph (see ElementKind.Text): the pen
  // starts at u (moved left for centre or right alignment) on baseline v and
  // advances by each character's width in the overlay font. No kerning.
  public static class TextLayout {
    public static double Width(string text) {
      double w = 0;
      foreach (char c in text) if (Printable(c)) w += OverlayFont.Advance(c);
      return w;
    }

    public static IEnumerable<Element> Glyphs(string text, float u, float v, float em, TextAlign align,
                                              uint color, byte priority, Eyes eyes, ElementFlags flags) {
      double width = Width(text) * em;
      double pen = align == TextAlign.Left ? u : align == TextAlign.Centre ? u - width / 2 : u - width;
      foreach (char c in text) {
        if (!Printable(c)) continue;
        float advance = (float)(OverlayFont.Advance(c) * em);
        if (c != ' ')
          yield return new Element {
            Kind = ElementKind.Text, Eyes = eyes, Priority = priority, Flags = flags,
            U = (float)pen, V = v, HalfW = advance / 2, HalfH = em, Color = color, Ref = c,
          };
        pen += advance;
      }
    }

    static bool Printable(char c) => c >= 32 && c <= 126;
  }
}
