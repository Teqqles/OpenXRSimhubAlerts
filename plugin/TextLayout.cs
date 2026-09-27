// plugin/TextLayout.cs
using System.Collections.Generic;
using OpenXRSimHubAlerts.Shared;

namespace OpenXRSimHubAlerts.Plugin {
  public enum TextAlign { Left, Centre, Right }

  // Lays a string out as one Text element per glyph (see ElementKind.Text): the pen
  // starts at u (moved left for centre or right alignment) on baseline v and
  // advances by each character's cell width in the overlay font. Digits all take
  // the widest digit's cell (see OverlayFont.DigitAdvance) so readouts do not
  // shift as their digits change; every other character keeps its natural
  // advance. No kerning.
  public static class TextLayout {
    public static double Width(string text) {
      double w = 0;
      foreach (char c in text) if (Printable(c)) w += Cell(c);
      return w;
    }

    public static IEnumerable<Element> Glyphs(string text, float u, float v, float em, TextAlign align,
                                              uint color, byte priority, Eyes eyes, ElementFlags flags) {
      if (!OverlayFont.Available) yield break;   // no font: nothing sane to lay out
      double width = Width(text) * em;
      double pen = align == TextAlign.Left ? u : align == TextAlign.Centre ? u - width / 2 : u - width;
      foreach (char c in text) {
        if (!Printable(c)) continue;
        float cell = (float)(Cell(c) * em);
        if (c != ' ') {
          float centring = IsDigit(c) ? (float)((OverlayFont.DigitAdvance - OverlayFont.Advance(c)) / 2 * em) : 0;
          yield return new Element {
            Kind = ElementKind.Text, Eyes = eyes, Priority = priority, Flags = flags,
            U = (float)pen + centring, V = v, HalfW = cell / 2, HalfH = em, Color = color, Ref = c,
          };
        }
        pen += cell;
      }
    }

    static double Cell(char c) => IsDigit(c) ? OverlayFont.DigitAdvance : OverlayFont.Advance(c);

    static bool IsDigit(char c) => c >= '0' && c <= '9';

    static bool Printable(char c) => c >= 32 && c <= 126;
  }
}
