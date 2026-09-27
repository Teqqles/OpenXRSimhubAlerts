// plugin/Icons.cs
using System.IO;
using OpenXRSimHubAlerts.Shared;

namespace OpenXRSimHubAlerts.Plugin {
  // The embedded icon PNGs (white, alpha is the shape), named after IconId in
  // snake case: ShiftUp -> shift_up.png. The layer embeds the same files.
  public static class Icons {
    public static Stream Open(IconId id) =>
      typeof(Icons).Assembly.GetManifestResourceStream($"OpenXRSimHubAlerts.Icons.{FileName(id)}.png");

    static string FileName(IconId id) {
      var name = new System.Text.StringBuilder();
      foreach (char c in id.ToString()) {
        if (char.IsUpper(c) && name.Length > 0) name.Append('_');
        name.Append(char.ToLowerInvariant(c));
      }
      return name.ToString();
    }
  }
}
