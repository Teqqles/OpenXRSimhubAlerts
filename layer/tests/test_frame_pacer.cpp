// layer/tests/test_frame_pacer.cpp
#include <catch2/catch_test_macros.hpp>
#include "frame_pacer.h"
#include "shm_contract.h"
#include <vector>

namespace {

constexpr int64_t kSec   = 1000000000;
constexpr int64_t kP90   = kSec / 90;
constexpr int64_t kP120  = kSec / 120;

// Feeds a FramePacer synthetic frames. `step` is the displayTime delta: `period`
// for a good frame, 2 * period for a missed one.
struct Sim {
  FramePacer pacer;
  int64_t    t = 1000 * kSec;  // arbitrary non-zero XrTime base

  bool Frame(uint8_t mode, int64_t period, int64_t step) {
    t += step;
    return pacer.ShouldRender(mode, t, period);
  }

  // Runs floor(seconds / step) frames; returns the frame indices that rendered.
  std::vector<int> RunSeconds(uint8_t mode, int64_t period, int64_t step, double seconds) {
    std::vector<int> rendered;
    const int64_t frames = static_cast<int64_t>(seconds * kSec) / step;
    for (int i = 0; i < frames; ++i)
      if (Frame(mode, period, step)) rendered.push_back(i);
    return rendered;
  }

  // Runs frames until exactly `n` more Auto windows have closed.
  void RunWindows(int64_t period, int64_t step, uint32_t n) {
    const uint32_t target = pacer.WindowsEvaluated() + n;
    while (pacer.WindowsEvaluated() < target) Frame(REFRESH_AUTO, period, step);
  }
  void Good(int64_t period, uint32_t n) { RunWindows(period, period, n); }
  void Bad(int64_t period, uint32_t n)  { RunWindows(period, 2 * period, n); }
};

}  // namespace

TEST_CASE("unlimited renders every frame and does no accounting", "[pacer]") {
  Sim s;
  auto r = s.RunSeconds(REFRESH_UNLIMITED, kP90, kP90, 3.0);
  REQUIRE(r.size() == 270);
  REQUIRE(s.pacer.WindowsEvaluated() == 0);
}

TEST_CASE("unknown mode behaves as unlimited", "[pacer]") {
  Sim s;
  REQUIRE(s.RunSeconds(200, kP90, kP90, 1.0).size() == 90);
}

TEST_CASE("fixed caps render the expected count over 10 s", "[pacer]") {
  struct Case { uint8_t mode; int fps; };
  const Case cases[] = {
    {REFRESH_60, 60}, {REFRESH_30, 30}, {REFRESH_15, 15},
    {REFRESH_10, 10}, {REFRESH_5, 5},   {REFRESH_1, 1},
  };
  for (int64_t period : {kP90, kP120}) {
    for (const Case& c : cases) {
      Sim s;
      const auto n = static_cast<int>(s.RunSeconds(c.mode, period, period, 10.0).size());
      INFO("period " << period << " fps " << c.fps << " rendered " << n);
      REQUIRE(n >= c.fps * 10 - 1);
      REQUIRE(n <= c.fps * 10 + 1);
    }
  }
}

TEST_CASE("60 fps on 120 Hz renders exactly every second frame", "[pacer]") {
  Sim s;
  auto r = s.RunSeconds(REFRESH_60, kP120, kP120, 2.0);
  for (size_t i = 1; i < r.size(); ++i) REQUIRE(r[i] - r[i - 1] == 2);
}

TEST_CASE("auto stays unlimited while frames are stable", "[pacer]") {
  Sim s;
  auto r = s.RunSeconds(REFRESH_AUTO, kP90, kP90, 5.0);
  REQUIRE(r.size() == 450);
  REQUIRE(s.pacer.AutoLevelFps() == 0);
  REQUIRE_FALSE(s.pacer.TakeStepChanged());
}

TEST_CASE("auto steps down per struggling window and floors at 10", "[pacer]") {
  Sim s;
  s.Frame(REFRESH_AUTO, kP90, kP90);  // enter Auto

  s.Bad(kP90, 1);
  REQUIRE(s.pacer.AutoLevelFps() == 30);
  REQUIRE(s.pacer.TakeStepChanged());
  REQUIRE_FALSE(s.pacer.TakeStepChanged());  // consumed

  s.Bad(kP90, 1);
  REQUIRE(s.pacer.AutoLevelFps() == 20);
  s.Bad(kP90, 1);
  REQUIRE(s.pacer.AutoLevelFps() == 10);
  s.pacer.TakeStepChanged();

  s.Bad(kP90, 1);
  REQUIRE(s.pacer.AutoLevelFps() == 10);
  REQUIRE_FALSE(s.pacer.TakeStepChanged());
}

TEST_CASE("auto render rate follows its level", "[pacer]") {
  Sim s;
  s.Frame(REFRESH_AUTO, kP90, kP90);
  s.Bad(kP90, 1);  // -> 30 fps
  REQUIRE(s.pacer.AutoLevelFps() == 30);
  // 5 s of good frames stays under the 10 s step-up threshold.
  const auto n = static_cast<int>(s.RunSeconds(REFRESH_AUTO, kP90, kP90, 5.0).size());
  REQUIRE(n >= 149);
  REQUIRE(n <= 151);
}

TEST_CASE("auto steps up only after 10 consecutive stable windows", "[pacer]") {
  Sim s;
  s.Frame(REFRESH_AUTO, kP90, kP90);
  s.Bad(kP90, 1);
  REQUIRE(s.pacer.AutoLevelFps() == 30);

  s.Good(kP90, 9);
  REQUIRE(s.pacer.AutoLevelFps() == 30);
  s.Good(kP90, 1);
  REQUIRE(s.pacer.AutoLevelFps() == 0);
}

TEST_CASE("a struggling window resets the step-up count", "[pacer]") {
  Sim s;
  s.Frame(REFRESH_AUTO, kP90, kP90);
  s.Bad(kP90, 1);   // 30
  s.Good(kP90, 5);
  s.Bad(kP90, 1);   // 20, count reset
  REQUIRE(s.pacer.AutoLevelFps() == 20);

  s.Good(kP90, 9);
  REQUIRE(s.pacer.AutoLevelFps() == 20);
  s.Good(kP90, 1);
  REQUIRE(s.pacer.AutoLevelFps() == 30);

  // The next step up needs another 10 stable windows.
  s.Good(kP90, 9);
  REQUIRE(s.pacer.AutoLevelFps() == 30);
  s.Good(kP90, 1);
  REQUIRE(s.pacer.AutoLevelFps() == 0);
}

TEST_CASE("auto evaluates the ladder once per 1 s window", "[pacer]") {
  Sim s;
  s.RunSeconds(REFRESH_AUTO, kP90, kP90, 5.5);
  REQUIRE(s.pacer.WindowsEvaluated() == 5);
}

TEST_CASE("mode change forces a render and resets auto", "[pacer]") {
  Sim s;
  s.Frame(REFRESH_AUTO, kP90, kP90);
  s.Bad(kP90, 3);
  REQUIRE(s.pacer.AutoLevelFps() == 10);

  REQUIRE(s.Frame(REFRESH_1, kP90, kP90));        // forced
  REQUIRE_FALSE(s.Frame(REFRESH_1, kP90, kP90));  // then capped

  REQUIRE(s.Frame(REFRESH_AUTO, kP90, kP90));     // forced again
  REQUIRE(s.pacer.AutoLevelFps() == 0);           // ladder reset
  REQUIRE(s.pacer.WindowsEvaluated() == 0);
}

TEST_CASE("headset period change does not count spurious misses", "[pacer]") {
  Sim s;
  s.Good(kP90, 3);
  s.Good(kP120, 3);
  s.Good(kP90, 3);
  REQUIRE(s.pacer.AutoLevelFps() == 0);
  REQUIRE_FALSE(s.pacer.TakeStepChanged());
}

TEST_CASE("invalid period renders every frame without counting misses", "[pacer]") {
  Sim s;
  REQUIRE(s.Frame(REFRESH_AUTO, 0, kP90));  // first frame renders
  // With period 0 no misses count, so Auto never steps down.
  auto r = s.RunSeconds(REFRESH_AUTO, 0, 2 * kP90, 3.0);
  REQUIRE(s.pacer.AutoLevelFps() == 0);
  REQUIRE(r.size() == 135);
}

TEST_CASE("urgent renders under a cap and keeps the cap's schedule", "[pacer]") {
  Sim s;
  REQUIRE(s.Frame(REFRESH_1, kP90, kP90));           // first frame renders
  REQUIRE_FALSE(s.Frame(REFRESH_1, kP90, kP90));     // capped
  s.t += kP90;
  REQUIRE(s.pacer.ShouldRender(REFRESH_1, s.t, kP90, /*urgent*/true));

  // The regular 1 fps render still lands one second after the first.
  int renders = 0;
  for (int i = 0; i < 88; ++i) renders += s.Frame(REFRESH_1, kP90, kP90) ? 1 : 0;
  REQUIRE(renders == 1);
}
