// plugin.tests/OverlayComposerTests.cs
using System;
using System.Linq;
using NUnit.Framework;
using OpenXRSimHubAlerts.Plugin;
using OpenXRSimHubAlerts.Shared;

public class OverlayComposerTests {
  // Shift lights off by default here so flag and radar tests see only their own elements.
  static Settings Base() => new Settings {
    EnableFlags = true, EnableRadar = true, ScaleFlag = 1f, RadarRange = 80f,
    PosFlagx = 0.8f, PosFlagy = 0.8f, EnableShiftLights = false,
  };

  static Element[] Compose(Settings s, FlagType flags, params CarBlip[] cars) =>
    Compose(s, flags, default(ShiftState), cars);

  static Element[] Compose(Settings s, FlagType flags, ShiftState shift, params CarBlip[] cars) {
    var block = new DataBlock();
    OverlayComposer.Compose(s, (byte)flags, cars, (uint)cars.Length, shift, ref block);
    return block.Elements.Take((int)block.ElementCount).ToArray();
  }

  static Settings Shift() {
    var s = Base(); s.EnableFlags = false; s.EnableRadar = false; s.EnableShiftLights = true;
    s.ShiftLightCount = 10; s.ShiftGlow = 0.5f; s.ShiftOpacity = 1f; s.ScaleShift = 1f;
    return s;
  }

  static CarBlip Car(float x, float y, float distance, byte side, byte flags = 0) =>
    new CarBlip { Rel = new Vec2 { X = x, Y = y }, Distance = distance, Side = side, Flags = flags };

  static byte R(uint c) => (byte)(c >> 16);
  static byte G(uint c) => (byte)(c >> 8);
  static byte B(uint c) => (byte)c;
  static byte A(uint c) => (byte)(c >> 24);

  [Test] public void NoFlagsOrCarsGivesNoElements() =>
    Assert.That(Compose(Base(), FlagType.None), Is.Empty);

  [Test] public void CopiesRefreshMode() {
    var s = Base(); s.RefreshMode = RefreshMode.Fps15;
    var block = new DataBlock();
    OverlayComposer.Compose(s, 0, new CarBlip[0], 0, default(ShiftState), ref block);
    Assert.That(block.RefreshMode, Is.EqualTo(RefreshMode.Fps15));
  }

  [Test] public void FlagPriorityPicksRedOverYellowInBothEyes() {
    var e = Compose(Base(), FlagType.Red | FlagType.Yellow);
    Assert.That(e.Select(x => x.Eyes), Is.EquivalentTo(new[] { Eyes.Left, Eyes.Right }));
    Assert.That(e.All(x => R(x.Color) > 200 && G(x.Color) < 80), "red, not yellow");
  }

  [Test] public void BarFlagSitsAtTheOuterEdgeOfEachEye() {
    var e = Compose(Base(), FlagType.Green);
    var left = e.Single(x => x.Eyes == Eyes.Left);
    var right = e.Single(x => x.Eyes == Eyes.Right);
    Assert.That(left.Kind, Is.EqualTo(ElementKind.Rect));
    Assert.That(left.U, Is.EqualTo(-0.82f).Within(1e-5));
    Assert.That(right.U, Is.EqualTo(0.82f).Within(1e-5));
    Assert.That(left.HalfH, Is.EqualTo(1f), "full height");
  }

  [Test] public void MeatballIsABlackFlagWithAnOrangeDot() {
    var s = Base(); s.EnableRadar = false; s.Shape = 3;   // circle marker
    var left = Compose(s, FlagType.Meatball).Where(x => x.Eyes == Eyes.Left).ToArray();
    Assert.That(left, Has.Length.EqualTo(2));
    Assert.That(R(left[0].Color) < 0x20 && G(left[0].Color) < 0x20, "black body first");
    Assert.That(R(left[1].Color) == 0xFF && G(left[1].Color) == 0x80 && B(left[1].Color) == 0, "orange dot on top");
    Assert.That(left[1].Kind, Is.EqualTo(ElementKind.Ellipse));
  }

  [Test] public void NonBarFlagMirrorsToTheOuterEdges() {
    var s = Base(); s.Shape = 2; s.PosFlagx = -0.5f;   // square, X sign ignored
    var e = Compose(s, FlagType.Blue);
    Assert.That(e.Single(x => x.Eyes == Eyes.Left).U, Is.EqualTo(-0.5f).Within(1e-5));
    Assert.That(e.Single(x => x.Eyes == Eyes.Right).U, Is.EqualTo(0.5f).Within(1e-5));
  }

  // The side radar blip overlaps the flag bar at the lens edge; the collision
  // warning must paint on top.
  [Test] public void RadarDrawsAboveFlags() {
    var e = Compose(Base(), FlagType.Yellow, Car(-3, 0, 3, 1));
    Assert.That(e.Count(x => x.Priority == OverlayComposer.FlagPriority), Is.EqualTo(2));
    Assert.That(e.Single(x => x.Priority == OverlayComposer.RadarPriority).Kind, Is.EqualTo(ElementKind.Rect));
    Assert.That(OverlayComposer.RadarPriority, Is.GreaterThan(OverlayComposer.FlagPriority));
  }

  [Test] public void FlagsDisabledEmitsNoFlag() {
    var s = Base(); s.EnableFlags = false;
    Assert.That(Compose(s, FlagType.Red), Is.Empty);
  }

  [Test] public void CarsAheadAndUnknownAreNotDrawn() {
    var s = Base(); s.EnableFlags = false;
    Assert.That(Compose(s, FlagType.None, Car(0, 10, 10, 3), Car(1, 1, 5, 0)), Is.Empty);
  }

  [Test] public void RadarDisabledEmitsNothing() {
    var s = Base(); s.EnableRadar = false; s.EnableFlags = false;
    Assert.That(Compose(s, FlagType.None, Car(3, 0, 3, 2)), Is.Empty);
  }

  [TestCase((byte)1, Eyes.Left)]
  [TestCase((byte)2, Eyes.Right)]
  [TestCase((byte)4, Eyes.Both)]
  public void RadarRoutesEachSideToItsEye(byte side, Eyes eyes) {
    var s = Base(); s.EnableFlags = false;
    Assert.That(Compose(s, FlagType.None, Car(side == 1 ? -3 : 3, side == 4 ? -3 : 0, 3, side)).Single().Eyes,
      Is.EqualTo(eyes));
  }

  [Test] public void RadarBlipsAreTimeCritical() {
    var s = Base(); s.EnableFlags = false;
    Assert.That(Compose(s, FlagType.None, Car(-3, 0, 3, 1)).Single().Flags, Is.EqualTo(ElementFlags.TimeCritical));
  }

  [Test] public void FlagsAreNotTimeCritical() =>
    Assert.That(Compose(Base(), FlagType.Red).All(x => x.Flags == ElementFlags.None));

  [Test] public void BlipSizeIgnoresTheClosestThreatFlag() {
    var s = Base(); s.EnableFlags = false;
    var threat = Compose(s, FlagType.None, Car(-3, 0, 3, 1, flags: 1)).Single();
    var normal = Compose(s, FlagType.None, Car(-3, 0, 3, 1, flags: 0)).Single();
    Assert.That(threat.HalfW, Is.EqualTo(normal.HalfW));
  }

  [Test] public void LeftCarRidesTheOuterRimVerticallyCentred() {
    var s = Base(); s.EnableFlags = false;
    var blip = Compose(s, FlagType.None, Car(-3, 0, 3, 1)).Single();
    Assert.That(blip.U, Is.LessThan(-0.7f));
    Assert.That(Math.Abs(blip.V), Is.LessThan(0.01f));
  }

  [Test] public void CarBehindSitsAtBottomCentre() {
    var s = Base(); s.EnableFlags = false;
    var blip = Compose(s, FlagType.None, Car(0, -3, 3, 4)).Single();
    Assert.That(Math.Abs(blip.U), Is.LessThan(0.01f));
    Assert.That(blip.V, Is.LessThan(-0.7f));
  }

  [Test] public void RadarRadiusClampKeepsBlipsOnLens() {
    var s = Base(); s.EnableFlags = false; s.ScaleRadar = 5f;
    var blip = Compose(s, FlagType.None, Car(-3, 0, 3, 1)).Single();
    Assert.That(blip.U, Is.EqualTo(-0.85f).Within(1e-5));
  }

  [Test] public void ArrowShapePointsAtTheCar() {
    var s = Base(); s.EnableFlags = false; s.RadarShape = 1;
    var blip = Compose(s, FlagType.None, Car(-3, 0, 3, 1)).Single();
    Assert.That(blip.Kind, Is.EqualTo(ElementKind.Triangle));
    // Left car: bearing -pi/2, arrow angle pi - bearing = 3pi/2 (apex pointing left).
    Assert.That(blip.Angle, Is.EqualTo(1.5 * Math.PI).Within(1e-5));
  }

  [Test] public void RadarBlipsAreShadesOfRed() {
    var s = Base(); s.EnableFlags = false;
    var c = Compose(s, FlagType.None, Car(-3, 0, 3, 1)).Single().Color;
    Assert.That(R(c), Is.GreaterThan(50));
    Assert.That(G(c), Is.Zero);
    Assert.That(B(c), Is.Zero);
  }

  [Test] public void RadarMaxOpacityCapsBlipAlpha() {
    var s = Base(); s.EnableFlags = false; s.RadarMaxOpacity = 0.5f;
    var near = Compose(s, FlagType.None, Car(0, -1, 0, 4)).Single();
    var far = Compose(s, FlagType.None, Car(0, -80, 80, 4)).Single();
    Assert.That(A(near.Color), Is.LessThanOrEqualTo(128));
    Assert.That(A(far.Color), Is.LessThan(A(near.Color)));
  }

  [Test] public void ElementCountNeverExceedsTheContract() {
    var s = Base();
    var cars = Enumerable.Range(0, ShmContract.MaxElements + 10).Select(_ => Car(0, -3, 3, 4)).ToArray();
    var block = new DataBlock();
    OverlayComposer.Compose(s, (byte)FlagType.Meatball, cars, (uint)cars.Length, default(ShiftState), ref block);
    Assert.That(block.ElementCount, Is.EqualTo(ShmContract.MaxElements));
  }

  static bool Critical(Element x) => (x.Flags & ElementFlags.TimeCritical) != 0;

  [Test] public void LitShiftLightsAreAGlowThenACore() {
    var e = Compose(Shift(), FlagType.None, new ShiftState { Lit = 3 });
    var lit = e.Where(Critical).ToArray();
    Assert.That(lit.Select(x => x.Kind), Is.EqualTo(new[] {
      ElementKind.Glow, ElementKind.Ellipse, ElementKind.Glow, ElementKind.Ellipse,
      ElementKind.Glow, ElementKind.Ellipse }));
    Assert.That(lit.All(x => x.Priority == OverlayComposer.ShiftPriority));
    Assert.That(lit[1].U, Is.LessThan(lit[3].U), "fills left to right");
    Assert.That(lit[0].HalfW, Is.GreaterThan(lit[1].HalfW), "halo is wider than the core");
  }

  [Test] public void EveryShiftLightIsForwardAnchored() {
    var e = Compose(Shift(), FlagType.None, new ShiftState { Lit = 4 });
    Assert.That(e, Is.Not.Empty);
    Assert.That(e.All(x => (x.Flags & ElementFlags.ForwardAnchored) != 0));
  }

  [Test] public void EvenRowSplitsLeftHalfLeftEyeRightHalfRightEye() {
    var cores = Compose(Shift(), FlagType.None, new ShiftState { Lit = 10 })
      .Where(x => x.Kind == ElementKind.Ellipse).ToArray();
    Assert.That(cores.Take(5).All(x => x.Eyes == Eyes.Left));
    Assert.That(cores.Skip(5).All(x => x.Eyes == Eyes.Right));
    Assert.That(cores[4].U, Is.LessThan(0f));
    Assert.That(cores[5].U, Is.GreaterThan(0f));
  }

  [Test] public void OddRowPutsTheMiddleLightInBothEyes() {
    var s = Shift(); s.ShiftLightCount = 5;
    var cores = Compose(s, FlagType.None, new ShiftState { Lit = 5 })
      .Where(x => x.Kind == ElementKind.Ellipse).ToArray();
    Assert.That(cores.Select(x => x.Eyes), Is.EqualTo(new[] {
      Eyes.Left, Eyes.Left, Eyes.Both, Eyes.Right, Eyes.Right }));
    Assert.That(cores[2].U, Is.EqualTo(0f).Within(1e-6));
  }

  [Test] public void LitColoursFollowTheBands() {
    var e = Compose(Shift(), FlagType.None, new ShiftState { Lit = 10 });
    var cores = e.Where(x => x.Kind == ElementKind.Ellipse).ToArray();
    Assert.That(cores, Has.Length.EqualTo(10));
    Assert.That(cores[0].Color, Is.EqualTo(ShiftLights.Green));
    Assert.That(cores[5].Color, Is.EqualTo(ShiftLights.Amber));
    Assert.That(cores[9].Color, Is.EqualTo(ShiftLights.Red));
  }

  [Test] public void UnlitLightsAreDimAndNotTimeCritical() {
    var e = Compose(Shift(), FlagType.None, new ShiftState { Lit = 3 });
    var unlit = e.Where(x => !Critical(x)).ToArray();
    Assert.That(unlit, Has.Length.EqualTo(7));
    Assert.That(unlit.All(x => x.Kind == ElementKind.Ellipse));
    Assert.That(R(unlit[6].Color), Is.LessThan(0x40), "red light dimmed");
  }

  [Test] public void UnlitLightsCanBeHidden() {
    var s = Shift(); s.ShowUnlitLights = false;
    Assert.That(Compose(s, FlagType.None, new ShiftState { Lit = 0 }), Is.Empty);
  }

  [Test] public void ZeroGlowEmitsNoHalo() {
    var s = Shift(); s.ShiftGlow = 0f;
    var e = Compose(s, FlagType.None, new ShiftState { Lit = 10 });
    Assert.That(e.Any(x => x.Kind == ElementKind.Glow), Is.False);
  }

  [Test] public void FlashOnPaintsEveryLightBlue() {
    var e = Compose(Shift(), FlagType.None, new ShiftState { Lit = 10, Flashing = true, FlashOn = true });
    var cores = e.Where(x => x.Kind == ElementKind.Ellipse).ToArray();
    Assert.That(cores, Has.Length.EqualTo(10));
    Assert.That(cores.All(x => x.Color == ShiftLights.Blue && Critical(x)));
  }

  [Test] public void FlashOffHasNoTimeCriticalLights() {
    var e = Compose(Shift(), FlagType.None, new ShiftState { Lit = 10, Flashing = true, FlashOn = false });
    Assert.That(e.Any(Critical), Is.False);
  }

  [Test] public void OpacityScalesCoreAlpha() {
    var s = Shift(); s.ShiftOpacity = 0.5f;
    var core = Compose(s, FlagType.None, new ShiftState { Lit = 1 }).First(x => x.Kind == ElementKind.Ellipse);
    Assert.That(A(core.Color), Is.EqualTo(128));
  }

  [Test] public void DisabledShiftLightsEmitNothing() {
    var s = Shift(); s.EnableShiftLights = false;
    Assert.That(Compose(s, FlagType.None, new ShiftState { Lit = 10 }), Is.Empty);
  }

  [Test] public void RowIsCentredOnItsPosition() {
    var s = Shift(); s.PosShiftx = 0.2f; s.PosShifty = 0.6f;
    var cores = Compose(s, FlagType.None, new ShiftState { Lit = 10 }).Where(x => x.Kind == ElementKind.Ellipse).ToArray();
    Assert.That((cores.First().U + cores.Last().U) / 2, Is.EqualTo(0.2f).Within(1e-5));
    Assert.That(cores.All(x => Math.Abs(x.V - 0.6f) < 1e-5));
  }
}
