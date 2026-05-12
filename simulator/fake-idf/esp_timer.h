#pragma once
// fake-idf stub of esp_timer.h for the desktop simulator.
// Provides esp_timer_get_time() (microseconds since an arbitrary start),
// backed by std::chrono so timings advance monotonically as on the device.
#include <cstdint>
#include <chrono>

static inline int64_t esp_timer_get_time(void) {
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    return duration_cast<microseconds>(steady_clock::now() - t0).count();
}
