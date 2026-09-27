#pragma once
#include <cstdint>

// Decides per xrEndFrame whether to redraw the overlay (DataBlock::refreshMode).
// Skipped frames resubmit the last released image. Times are displayTime in ns.
//
// Auto runs unlimited until a 1 s window has more than 10% missed frames
// (displayTime delta > 1.5 x period), then steps down to 30, 20 and 10 fps.
// Ten stable windows in a row step back up one level.
class FramePacer {
public:
  // `period` is predictedDisplayPeriod, or <= 0 if unknown. `urgent` renders
  // now without moving the cap's schedule.
  bool ShouldRender(uint8_t mode, int64_t displayTime, int64_t period, bool urgent = false);

  // Auto cap in fps; 0 = unlimited.
  int AutoLevelFps() const;

  // True once per Auto step change.
  bool TakeStepChanged();

  // Auto windows closed since the last mode change.
  uint32_t WindowsEvaluated() const { return _windowsEvaluated; }

private:
  void Reset(uint8_t mode);
  int  AutoFps(int64_t displayTime, int64_t period);
  void CloseWindow();

  uint8_t  _mode        = 0xFF;  // forces a reset on the first call
  bool     _forceRender = true;
  int64_t  _nextDue     = 0;     // displayTime the next capped render is due

  // Auto state.
  bool     _havePrev         = false;
  int64_t  _prevDisplay      = 0;
  bool     _windowOpen       = false;
  int64_t  _windowEnd        = 0;
  uint32_t _frames           = 0;
  uint32_t _misses           = 0;
  int      _stableWindows    = 0;
  int      _level            = 0;  // index into the Auto ladder
  bool     _stepChanged      = false;
  uint32_t _windowsEvaluated = 0;
};
