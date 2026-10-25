// From the repo root: g++ -std=c++17 -I. test/pulse_test.cpp -o /tmp/pulse_test && /tmp/pulse_test
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include "pulse.h"

// Feeds level(t) once per ms (one loop() pass) for `ms` ms, returns the counted pulses.
template <typename F>
int run(PulseDebouncer& d, uint32_t& t, uint32_t ms, F level) {
  int pulses = 0;
  for (uint32_t end = t + ms; t < end; t++) pulses += d.update(level(t));
  return pulses;
}

int main() {
  // 50 Hz hum: contact pulled low for 5 ms of every 20 ms. Never counts.
  {
    PulseDebouncer d;
    uint32_t t = 1;
    assert(run(d, t, 60000, [](uint32_t t) { return t % 20 < 5; }) == 0);
    assert(d.edges > 1000);
  }
  // Even 50 % hum (10 ms low, 10 ms high) never counts.
  {
    PulseDebouncer d;
    uint32_t t = 1;
    assert(run(d, t, 60000, [](uint32_t t) { return t % 20 < 10; }) == 0);
  }
  // Single short spikes (1 ms low every 300 ms, like the old phantom counts). Never counts.
  {
    PulseDebouncer d;
    uint32_t t = 1;
    assert(run(d, t, 60000, [](uint32_t t) { return t % 300 == 0; }) == 0);
  }
  // Real reed at max flow: 1200 ms period, closed for 300 ms, with bounce at both edges.
  {
    PulseDebouncer d;
    uint32_t t = 1;
    auto reed = [](uint32_t t) {
      uint32_t p = t % 1200;
      if (p < 10) return p % 2 == 0;            // bounce while closing
      if (p >= 300 && p < 310) return p % 2 == 1;  // bounce while opening
      return p < 300;
    };
    assert(run(d, t, 12000, reed) == 10);
  }
  // Two wires held together by hand: closed, but open 20 % of the time at random. Once per touch.
  {
    PulseDebouncer d;
    uint32_t t = 1;
    srand(1);
    for (int touch = 0; touch < 4; touch++) {
      assert(run(d, t, 2000, [](uint32_t) { return rand() % 5 != 0; }) == 1);
      assert(run(d, t, 2000, [](uint32_t) { return false; }) == 0);
    }
  }
  // Contact held closed for a long time (slow flow) counts once.
  {
    PulseDebouncer d;
    uint32_t t = 1;
    assert(run(d, t, 10000, [](uint32_t) { return true; }) == 1);
    assert(run(d, t, 10000, [](uint32_t) { return false; }) == 0);
    assert(run(d, t, 100, [](uint32_t) { return true; }) == 1);
  }
  // Bad hum on top of a real pulse: the contact still counts once.
  {
    PulseDebouncer d;
    uint32_t t = 1;
    assert(run(d, t, 2000, [](uint32_t t) { return t % 20 < 5; }) == 0);
    assert(run(d, t, 300, [](uint32_t) { return true; }) == 1);
    assert(run(d, t, 2000, [](uint32_t t) { return t % 20 < 5; }) == 0);
  }
  puts("pulse_test ok");
}
