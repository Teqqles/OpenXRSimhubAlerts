#include "frame_pacer.h"
#include "shm_contract.h"

namespace {

constexpr int64_t kNsPerSec              = 1000000000;
constexpr int64_t kWindowNs              = kNsPerSec;  // Auto evaluation window
constexpr int     kStableWindowsToStepUp = 10;

// Auto ladder in fps, top first; 0 = unlimited.
constexpr int kAutoLadder[] = {0, 30, 20, 10};
constexpr int kAutoLevels   = sizeof(kAutoLadder) / sizeof(kAutoLadder[0]);

// Fixed-mode cap in fps; 0 = unlimited (also for unknown modes).
int FixedFps(uint8_t mode) {
  switch (mode) {
    case REFRESH_60: return 60;
    case REFRESH_30: return 30;
    case REFRESH_15: return 15;
    case REFRESH_10: return 10;
    case REFRESH_5:  return 5;
    case REFRESH_1:  return 1;
    default:         return 0;
  }
}

// More than 1.5 periods since the previous frame means a refresh was missed.
bool IsMiss(int64_t delta, int64_t period) { return 2 * delta > 3 * period; }

}  // namespace

bool FramePacer::ShouldRender(uint8_t mode, int64_t displayTime, int64_t period) {
  if (mode != _mode) Reset(mode);

  const int fps = (_mode == REFRESH_AUTO) ? AutoFps(displayTime, period) : FixedFps(_mode);
  const int64_t halfPeriod = period > 0 ? period / 2 : 0;

  // The half-period tolerance snaps slots to whole frames (60 fps at 120 Hz
  // draws every second frame).
  if (!_forceRender && fps != 0 && displayTime <= _nextDue - halfPeriod) return false;
  _forceRender = false;

  if (fps == 0) {
    _nextDue = displayTime;
  } else {
    // Step by whole intervals to hold the average rate; after a stall, restart
    // from now.
    const int64_t interval = kNsPerSec / fps;
    const bool    behind   = displayTime - _nextDue > interval;
    _nextDue = behind ? displayTime + interval : _nextDue + interval;
  }
  return true;
}

int FramePacer::AutoLevelFps() const { return kAutoLadder[_level]; }

bool FramePacer::TakeStepChanged() {
  const bool changed = _stepChanged;
  _stepChanged = false;
  return changed;
}

void FramePacer::Reset(uint8_t mode) {
  *this = FramePacer{};
  _mode = mode;
}

int FramePacer::AutoFps(int64_t displayTime, int64_t period) {
  if (!_windowOpen) {
    _windowOpen = true;
    _windowEnd  = displayTime + kWindowNs;
  }
  // Count misses only with a previous frame and a known period.
  if (_havePrev && period > 0) {
    ++_frames;
    if (IsMiss(displayTime - _prevDisplay, period)) ++_misses;
  }
  _prevDisplay = displayTime;
  _havePrev    = true;

  if (displayTime >= _windowEnd) {
    CloseWindow();
    _windowEnd = displayTime + kWindowNs;
  }
  return kAutoLadder[_level];
}

void FramePacer::CloseWindow() {
  ++_windowsEvaluated;
  const bool struggling = _misses * 10 > _frames;  // > 10% missed
  _frames = 0;
  _misses = 0;

  if (struggling) {
    _stableWindows = 0;
    if (_level < kAutoLevels - 1) {
      ++_level;
      _stepChanged = true;
    }
    return;
  }
  if (++_stableWindows >= kStableWindowsToStepUp) {
    _stableWindows = 0;
    if (_level > 0) {
      --_level;
      _stepChanged = true;
    }
  }
}
