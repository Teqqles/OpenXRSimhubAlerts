// shared/ShmContract.cs
using System;
using System.Runtime.InteropServices;

namespace OpenXRSimHubAlerts.Shared {
  public static class ShmContract {
    public const string Name = "OpenXRSimHubAlerts";
    public const uint Version = 6;
    public const int MaxElements = 128;
  }

  // Overlay re-render rate (DataBlock.RefreshMode). Mirrors RefreshMode in shm_contract.h.
  public enum RefreshMode : byte {
    Unlimited=0, Auto=1, Fps60=2, Fps30=3, Fps15=4, Fps10=5, Fps5=6, Fps1=7
  }

  // Mirrors ElementKind in shm_contract.h. Glow is an ellipse that fades from its
  // colour at the centre to transparent at the rim. Text is one glyph: ref is its
  // ASCII code (32 to 126), u is the pen x (glyph origin), v the baseline, hh the
  // font size (em) and hw half the advance. Icon fills the box u +/- hw, v +/- hh;
  // ref is an IconId.
  public enum ElementKind : byte { None=0, Rect=1, Ellipse=2, Triangle=3, Text=4, Icon=5, Glow=6 }

  // Element.Ref for icons. Mirrors IconId in shm_contract.h.
  public enum IconId : ushort { Fuel=0, Abs=1, Tc=2, Drs=3, ShiftUp=4, ShiftDown=5 }

  [Flags] public enum Eyes : byte { None=0, Left=1, Right=2, Both=3 }

  [Flags] public enum ElementFlags : byte {
    None=0,
    TimeCritical=1,     // appearing or disappearing bypasses the refresh cap
    ForwardAnchored=2,  // u measured from straight ahead, not the eye's FOV centre
  }

  // One drawable shape. Positions and sizes are NDC per eye (y up).
  [StructLayout(LayoutKind.Sequential, Pack=4)]
  public struct Element {
    public ElementKind Kind;
    public Eyes Eyes;
    public byte Priority;        // drawn lowest first; higher paints on top
    public ElementFlags Flags;
    public float U, V;           // centre
    public float HalfW, HalfH;
    public float Angle;          // radians clockwise (triangles)
    public uint Color;           // 0xAARRGGBB
    public ushort Ref;           // text: ASCII code; icon: IconId
    public ushort Pad;
  }

  [StructLayout(LayoutKind.Sequential, Pack=4)]
  public struct DataBlock {
    public uint Version, Seq;
    public byte Connected;
    public RefreshMode RefreshMode;
    public byte Pad0, Pad1;
    public uint ElementCount;
    [MarshalAs(UnmanagedType.ByValArray, SizeConst=ShmContract.MaxElements)] public Element[] Elements;
  }
}
