/*
 * MIT License
 * Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "timer.h"
#include <time.h>

//C23: timespec_get is mandatory, TIME_UTC maps to the
//highest-resolution wall clock (QPC-based on MSVC, same as chrono).
int64_t get_nanoseconds(void) {
	struct timespec ts;
	timespec_get(&ts, TIME_UTC);
	return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

int64_t get_microseconds(void) {
	struct timespec ts;
	timespec_get(&ts, TIME_UTC);
	return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

int64_t get_milliseconds(void) {
	struct timespec ts;
	timespec_get(&ts, TIME_UTC);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int64_t get_seconds(void) {
	struct timespec ts;
	timespec_get(&ts, TIME_UTC);
	return (int64_t)ts.tv_sec;
}

int64_t get_utc_milliseconds(void) {
	struct timespec ts;
	timespec_get(&ts, TIME_UTC);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
