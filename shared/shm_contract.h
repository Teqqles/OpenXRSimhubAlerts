// shared/shm_contract.h
#pragma once
#include <cstdint>

#define SHM_NAME "OpenXRSimHubAlerts"
#define SHM_VERSION 5u
#define MAX_ELEMENTS 128

// Overlay re-render rate (DataBlock::refreshMode). Unknown values = unlimited.
// Mirrored in ShmContract.cs.
enum RefreshMode : uint8_t {
  REFRESH_UNLIMITED=0, REFRESH_AUTO=1, REFRESH_60=2, REFRESH_30=3,
  REFRESH_15=4, REFRESH_10=5, REFRESH_5=6, REFRESH_1=7
};

// Element::kind. Text and icon are reserved (#4). Glow is an ellipse that fades
// from its colour at the centre to transparent at the rim.
enum ElementKind : uint8_t {
  ELEMENT_NONE=0, ELEMENT_RECT=1, ELEMENT_ELLIPSE=2, ELEMENT_TRIANGLE=3,
  ELEMENT_TEXT=4, ELEMENT_ICON=5, ELEMENT_GLOW=6
};

// Element::eyes bits.
enum EyeMask : uint8_t { EYE_LEFT=1, EYE_RIGHT=2, EYE_BOTH=3 };

// Element::flags bits. A time-critical element appearing or disappearing
// bypasses the refresh cap. A forward-anchored element's u is measured from the
// head's straight-ahead direction in that eye, not from the centre of the eye's FOV.
enum ElementFlags : uint8_t { ELEMENT_TIME_CRITICAL=1, ELEMENT_FORWARD_ANCHORED=2 };

#pragma pack(push, 4)
// One drawable shape. Positions and sizes are NDC per eye (y up).
struct Element {
  uint8_t  kind;
  uint8_t  eyes;
  uint8_t  priority;   // drawn lowest first; higher paints on top
  uint8_t  flags;
  float    u, v;       // centre
  float    hw, hh;     // half-size
  float    angle;      // radians clockwise (triangles)
  uint32_t color;      // 0xAARRGGBB
  uint16_t ref;        // text or icon id (#4)
  uint16_t _pad;
};

struct DataBlock {
  uint32_t version;
  uint32_t seq;
  uint8_t  connected;
  uint8_t  refreshMode;  // RefreshMode
  uint8_t  _pad[2];
  uint32_t elementCount;
  Element  elements[MAX_ELEMENTS];
};
#pragma pack(pop)
