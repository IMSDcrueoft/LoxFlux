/*
 * MIT License
 * Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "path.h"
#include <filesystem>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

char* getAbsolutePath(const char* pathIn) {
    if (!pathIn) return nullptr;

    std::error_code ec;
    fs::path inputPath(pathIn);

    // Check existence
    if (!fs::exists(inputPath, ec) || ec) return nullptr;

    // Get and normalize absolute path
    fs::path absolutePath = fs::absolute(inputPath, ec).lexically_normal();
    if (ec) return nullptr;

    std::string absPathStr = absolutePath.u8string();

    // Allocate memory with explicit null terminator space
    char* pathOut = new (std::nothrow) char[absPathStr.size() + 1]; // +1 for '\0'
    if (!pathOut) return nullptr;

    std::memcpy(pathOut, absPathStr.c_str(), absPathStr.size() + 1); // safe & fast copy
    return pathOut;
}

void freePathOut(char** const pathOut) {
	if (pathOut && *pathOut) {
		delete[] * pathOut; // new[] with delete[]
		*pathOut = nullptr;
	}
}