/*
 * MIT License
 * Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "timer.h"
#include <chrono>

int64_t get_nanoseconds() {
	auto now = std::chrono::high_resolution_clock::now();
	return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
}

int64_t get_microseconds() {
	auto now = std::chrono::high_resolution_clock::now();
	return std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
}

int64_t get_milliseconds() {
	auto now = std::chrono::high_resolution_clock::now();
	return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

int64_t get_seconds()
{
	auto now = std::chrono::high_resolution_clock::now();
	return std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
}

int64_t get_utc_milliseconds() {
	auto now = std::chrono::system_clock::now();
	return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}