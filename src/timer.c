/*
 * MIT License
 * Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include <inttypes.h>
#include <time.h>

#if defined(_WIN32)
#define PLATFORM_WINDOWS
#include <windows.h>
#elif defined(__linux__)
#define PLATFORM_LINUX
#include <time.h>
#elif defined(__ANDROID__)
#define PLATFORM_ANDROID
#include <time.h>
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE || TARGET_OS_SIMULATOR
#define PLATFORM_IOS
#include <mach/mach_time.h>
#else
#define PLATFORM_MACOS
#include <mach/mach_time.h>
#endif
#elif defined(__EMSCRIPTEN__)
#define PLATFORM_EMSCRIPTEN
#include <emscripten.h>
#else
#error "Unsupported platform"
#endif
#include "timer.h"

uint64_t get_nanoseconds() {
    uint64_t ns = 0;

#ifdef PLATFORM_WINDOWS
    // Windows:  QueryPerformanceCounter
    static LARGE_INTEGER frequency = {0};
    static int initialized = 0;
    if (!initialized) {
        if (QueryPerformanceFrequency(&frequency) == 0) {
            return 0;
        }
        initialized = 1;
    }

    LARGE_INTEGER counter;
    if (QueryPerformanceCounter(&counter) == 0) {
        return 0;
    }
    ns = (uint64_t)((double)counter.QuadPart / frequency.QuadPart * 1e9);

#elif defined(PLATFORM_LINUX) || defined(PLATFORM_ANDROID)
    // Linux/Android:  clock_gettime
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
        return 0;
    }
    ns = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;

#elif defined(PLATFORM_MACOS) || defined(PLATFORM_IOS)
    // macOS/iOS:  mach_absolute_time
    static mach_timebase_info_data_t timebase = {0};
    static int initialized = 0;
    if (!initialized) {
        if (mach_timebase_info(&timebase)!= KERN_SUCCESS) {
            return 0;
        }
        initialized = 1;
    }

    uint64_t time = mach_absolute_time();
    ns = time * timebase.numer / timebase.denom;

#elif defined(PLATFORM_EMSCRIPTEN)
    // Emscripten:  emscripten_get_now
    double now_ms = emscripten_get_now();
    if (now_ms < 0) {
        return 0;
    }
    ns = (uint64_t)(now_ms * 1e6);

#endif

    return ns;
}

uint64_t get_utc_milliseconds() {
    uint64_t ms = 0;

#ifdef PLATFORM_WINDOWS
    // Windows:  GetSystemTimeAsFileTime
    FILETIME fileTime;
    GetSystemTimeAsFileTime(&fileTime);
    uint64_t utc = ((uint64_t)fileTime.dwHighDateTime << 32) | fileTime.dwLowDateTime;
    ms = (uint64_t)(utc / 10); // 100 ns to ms

#elif defined(PLATFORM_LINUX) || defined(PLATFORM_ANDROID)
    // Linux/Android:  gettimeofday
    struct timeval tv;
    gettimeofday(&tv, NULL);
    ms = (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;

#elif defined(PLATFORM_MACOS) || defined(PLATFORM_IOS)
    // macOS/iOS:  clock_gettime
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ms = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

#elif defined(PLATFORM_EMSCRIPTEN)
    // Emscripten:  emscripten_get_now
    double now_ms = emscripten_get_now();
    if (now_ms < 0) {
        return 0;
    }
    ms = (uint64_t)now_ms; // sec

#endif

    return ms;
}

#ifdef PLATFORM_WINDOWS
#undef PLATFORM_WINDOWS
#endif
#ifdef PLATFORM_LINUX
#undef PLATFORM_LINUX
#endif
#ifdef PLATFORM_ANDROID
#undef PLATFORM_ANDROID
#endif
#ifdef PLATFORM_MACOS
#undef PLATFORM_MACOS
#endif
#ifdef PLATFORM_IOS
#undef PLATFORM_IOS
#endif
#ifdef PLATFORM_EMSCRIPTEN
#undef PLATFORM_EMSCRIPTEN
#endif