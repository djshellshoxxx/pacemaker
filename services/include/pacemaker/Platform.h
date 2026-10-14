// Tiny platform helpers shared by the output threads.
#pragma once
#if defined(_WIN32)
 #define WIN32_LEAN_AND_MEAN
 #define NOMINMAX
 #include <windows.h>
 #include <mmsystem.h>
#endif

namespace pacemaker {

// 1 ms system timer resolution on Windows for the lifetime of the object; no-op elsewhere.
struct ScopedTimerResolution {
#if defined(_WIN32)
    ScopedTimerResolution() { timeBeginPeriod(1); }
    ~ScopedTimerResolution() { timeEndPeriod(1); }
#else
    ScopedTimerResolution() = default;
#endif
};

} // namespace pacemaker
