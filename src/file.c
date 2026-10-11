/*
 * MIT License
 * Copyright (c) 2025-2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "file.h"
#include "allocator.h"

// Platform detection
#ifdef _WIN32
#include <windows.h>
#include <direct.h> // for _fullpath
#define PATH_MAX MAX_PATH
#else
#include <limits.h> // for PATH_MAX
#include <unistd.h> // for realpath
#endif

/**
 * @brief Get the absolute and normalized path of a given path.
 *
 * The caller is responsible for freeing the returned buffer using free().
 *
 * @param path Input relative or local path string.
 * @return char* Dynamically allocated string containing the absolute path, or NULL on failure.
 */
STR getAbsolutePath(C_STR path) {
	if (!path) return NULL;

	char resolvedPath[PATH_MAX] = { 0 };

#ifdef _WIN32
	// Windows: use _fullpath to resolve absolute path
	if (_fullpath(resolvedPath, path, PATH_MAX) == NULL) {
		return NULL; // Error already set by _fullpath
	}
#else
	// POSIX: use realpath to resolve absolute path (also resolves symlinks)
	if (realpath(path, resolvedPath) == NULL) {
		return NULL; // Error already set by realpath
	}
#endif

	// Allocate memory for the result and copy
	size_t len = strlen(resolvedPath);
	STR result = (STR)mem_alloc(len + 1);

	if (!result) {
		fprintf(stderr, "Not enough memory to allocate.\n");
		exit(74);
	}

	memcpy(result, resolvedPath, len + 1);
	return result;
}

STR readFile(C_STR path) {
	FILE* file = fopen(path, "rb");

	if (file == NULL) {
		fprintf(stderr, "Could not open file \"%s\".\n", path);
		exit(74);
	}

	fseek(file, 0L, SEEK_END);
	uint64_t fileSize = ftell(file);
	rewind(file);

	STR buffer = (STR)mem_alloc(fileSize + 1);
	
	if (buffer == NULL) {
		fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
		exit(74);
	}

	uint64_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
	fclose(file);

	if (bytesRead < fileSize) {
		fprintf(stderr, "Could not read file \"%s\".\n", path);
		exit(74);
	}

	buffer[bytesRead] = '\0';

	//UTF-8 BOM: strip it, the scanner is byte-transparent for the rest
	if (bytesRead >= 3 && (uint8_t)buffer[0] == 0xEF && (uint8_t)buffer[1] == 0xBB && (uint8_t)buffer[2] == 0xBF) {
		memmove(buffer, buffer + 3, bytesRead - 3 + 1);//+1 keeps the '\0'
	}
	//UTF-32 BOMs: the payload is not byte-oriented UTF-8, scanning it would only
	//produce garbage; refuse with a clear message instead of failing mysteriously
	else if (bytesRead >= 4 && (((uint8_t)buffer[0] == 0xFF && (uint8_t)buffer[1] == 0xFE && (uint8_t)buffer[2] == 0x00 && (uint8_t)buffer[3] == 0x00) ||
		((uint8_t)buffer[0] == 0x00 && (uint8_t)buffer[1] == 0x00 && (uint8_t)buffer[2] == 0xFE && (uint8_t)buffer[3] == 0xFF))) {
		fprintf(stderr, "Cannot read \"%s\": UTF-32 encoded source is not supported, re-save it as UTF-8.\n", path);
		exit(74);
	}
	//UTF-16 BOMs: same reasoning as UTF-32 above
	else if (bytesRead >= 2 && (((uint8_t)buffer[0] == 0xFF && (uint8_t)buffer[1] == 0xFE) || ((uint8_t)buffer[0] == 0xFE && (uint8_t)buffer[1] == 0xFF))) {
		fprintf(stderr, "Cannot read \"%s\": UTF-16 encoded source is not supported, re-save it as UTF-8.\n", path);
		exit(74);
	}

	return buffer;
}