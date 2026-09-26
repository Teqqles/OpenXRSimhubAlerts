// shared/shm_contract.h
#pragma once
#include <cstdint>

#define SHM_NAME "OpenXRSimHubAlerts"
#define SHM_VERSION 3u
#define MAX_CARS 64

#pragma pack(push, 4)
struct Vec2 { float x, y; };

enum FlagType : uint8_t {
  FLAG_NONE=0, FLAG_GREEN=1, FLAG_YELLOW=2, FLAG_BLUE=4,
  FLAG_WHITE=8, FLAG_RED=16, FLAG_BLACK=32, FLAG_MEATBALL=64
};

// Overlay re-render rate (Config::refreshMode). Unknown values = unlimited.
// Mirrored in ShmContract.cs.
enum RefreshMode : uint8_t {
  REFRESH_UNLIMITED=0, REFRESH_AUTO=1, REFRESH_60=2, REFRESH_30=3,
  REFRESH_15=4, REFRESH_10=5, REFRESH_5=6, REFRESH_1=7
};

struct CarBlip {
  Vec2    rel;
  float   distance;
  uint8_t side;    // 0 none,1 left,2 right,3 ahead,4 behind
  uint8_t flags;   // bit0 = closest threat on its side
  uint8_t _pad[2];
};

struct Config {
  uint8_t  shape;       // flag shape: 0 bar,1 rect,2 square,3 circle,4 triangle
  uint8_t  radarShape;  // radar shape: 0 car (full-height edge bar), 1 arrow (points at car)
  uint8_t  flagCorner;
  uint8_t  enableFlags;
  uint8_t  enableRadar;
  uint8_t  refreshMode; // RefreshMode
  uint8_t  _pad0[2];
  float    scaleL, scaleR, scaleFlag;
  float    scaleRadar;       // radar blip size multiplier
  float    flagOpacity;      // 0..1 flag alpha
  float    radarMaxOpacity;  // 0..1 radar alpha ceiling; closeness scales up to this
  Vec2     posL, posR, posFlag;
  float    radarRange;
  uint32_t colorOverride[8];
};

struct DataBlock {
  uint32_t version;
  uint32_t seq;
  uint8_t  connected;
  uint8_t  activeFlags;
  uint8_t  _pad[2];
  uint32_t carCount;
  CarBlip  cars[MAX_CARS];
  Config   config;
};
#pragma pack(pop)
