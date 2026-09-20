using OpenXRSimHubAlerts.Shared;

namespace OpenXRSimHubAlerts.Plugin {
  public struct FlagInput {
    public bool Green, Yellow, Blue, White, Red, Black, Meatball;
  }
  public static class FlagMapper {
    public static byte Map(FlagInput i) {
      byte f = 0;
      if (i.Green)    f |= (byte)FlagType.Green;
      if (i.Yellow)   f |= (byte)FlagType.Yellow;
      if (i.Blue)     f |= (byte)FlagType.Blue;
      if (i.White)    f |= (byte)FlagType.White;
      if (i.Red)      f |= (byte)FlagType.Red;
      if (i.Black)    f |= (byte)FlagType.Black;
      if (i.Meatball) f |= (byte)FlagType.Meatball;
      return f;
    }
  }
}
