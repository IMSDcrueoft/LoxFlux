/*
 * MIT License
 * Copyright (c) 2025 IM&SD (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "entrance.h"
#include "version.h"
#include "vm.h"

static void print_help() {
	printf("Commands:\n");
	printf("/exit  - Exit the interpreter.\n");
	printf("/eval  - Load file and run.\n");
	printf("/mem   - Print memory statistics.\n");
	printf("/help  - Print this help message.\n");
	printf("/clear - Clean console.\n");
	printf("\nAbout:\n");
	printf("input \'\\\' to enter next line, with 512 characters maximum per line.\n");
}

static void clear_console() {
#ifdef _WIN32
	system("cls"); // Windows
#elif defined(__unix__) || defined(__linux__) || defined(__APPLE__)
	printf("\033[2J\033[1;1H"); // ANSI Unix like
#else
	for (uint32_t i = 0; i < 5; i++) {
		printf("\n\n\n\n\n\n\n\n\n\n");
	}
#endif
}

static STR readFile(C_STR path) {
	FILE* file = fopen(path, "rb");

	if (file == NULL) {
		fprintf(stderr, "Could not open file \"%s\".\n", path);
		exit(74);
	}

	fseek(file, 0L, SEEK_END);
	size_t fileSize = ftell(file);
	rewind(file);

	STR buffer = (STR)malloc(fileSize + 1);

	if (buffer == NULL) {
		fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
		exit(74);
	}

	size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);

	if (bytesRead < fileSize) {
		fprintf(stderr, "Could not read file \"%s\".\n", path);
		exit(74);
	}

	buffer[bytesRead] = '\0';

	fclose(file);
	return buffer;
}

static STR dealWithFilePath(STR line) {
	STR begin = line;
	// skip whiteSpace and tab
	while (*begin == ' ' || *begin == '\t') {
		++begin;
	}

	if (*begin == '"') {
		++begin;
	}

	STR back = begin;

	while (*back != '\0') {
		if (*back == '"' || *back == '\n') {
			*back = '\0';
			break;
		}
		++back;
	}

	//no '"'
	if (*begin == '\0') {
		begin = line;

		while (*begin == ' ' || *begin == '\t') {
			++begin;
		}

		back = begin;
		while (*back != '\0' && *back != '\n') {
			++back;
		}
		*back = '\0';
	}

	back = begin + strlen(begin) - 1;
	while (back >= begin && (*back == ' ' || *back == '\t')) {
		*back = '\0';
		--back;
	}

	return begin;
}

void repl() {
	printf("%s %s  Copyright (C) %s, %s\n", INTERPRETER_NAME, INTERPRETER_VERSION, INTERPRETER_COPYRIGHT, INTERPRETER_OWNER);

	vm_init();

	char line[256];
	STR fullLine = (STR)malloc(512);
	STR result = NULL;

	uint32_t fullLineLength = 0;
	uint32_t fullLineCapacity = 512;
	uint32_t lineLen = 0;

	bool isContinued;

#define startsWith_string(a,b) (strncmp(a,b,strlen(b)) == 0)
#define match_string(a,b) (startsWith_string(a,b) && (a[strlen(b)] == '\0' || a[strlen(b)] == '\n'))

	/*
	* input '\' to enter next line
	* input "/exit" to exit
	*/

	while (true) {
		printf("> ");

		// get one line
		if (!fgets(line, sizeof(line), stdin)) {
			printf("\n");
			break;
		}

		lineLen = (uint32_t)strlen(line);
		if (lineLen > 1 && line[lineLen - 2] == '\\') {
			line[lineLen - 2] = '\n';
			line[lineLen - 1] = '\0';

			isContinued = true;
		}
		else if (lineLen > 0) {
			if (line[0] == '\n') continue;
			isContinued = false;

			//check '/exit'
			if (line[0] == '/') {
				if (match_string(line, "/exit")) {
					break;
				}
				else if (match_string(line, "/mem")) {
					log_malloc_info();
					continue;
				}
				else if (match_string(line, "/help")) {
					print_help();
					continue;
				}
				else if (match_string(line, "/clear")) {
					clear_console();
					continue;
				}
				else if (startsWith_string(line, "/eval ")) {
					STR path = dealWithFilePath(line + 6);
					STR source = readFile(path);
					interpret_repl(source);
					free(source);
					continue;
				}
			}
		}
		else {
			continue;
		}

		if ((fullLineLength + lineLen + 1) >= fullLineCapacity) {
			result = (STR)realloc(fullLine, fullLineCapacity += 1024);
			if (result == NULL) {
				fprintf(stderr, "Memory reallocation failed!\n");
				exit(1);
			}
			fullLine = result;
			result = NULL;
		}

		//be safe
		if ((fullLine + fullLineLength) == NULL) {
			exit(1);
		}

		memcpy(fullLine + fullLineLength, line, lineLen);
		fullLineLength += (lineLen - 1);
		fullLine[fullLineLength] = '\0';

		if (!isContinued) {
#if DEBUG_MODE
			printf("CODE TEXT:\n%s\n", fullLine);
#endif

			interpret_repl(fullLine);
			fullLine[0] = '\0';
			fullLineLength = 0;
		}
	}

	free(fullLine);

	vm_free();

#if LOG_MALLOC_INFO
	log_malloc_info();
#endif
#undef match_string
}

void runFile(C_STR path) {
	vm_init();

	STR source = readFile(path);
	InterpretResult result = interpret(source);
	free(source);

	if (result == INTERPRET_COMPILE_ERROR) exit(65);
	if (result == INTERPRET_RUNTIME_ERROR) exit(70);

	vm_free();

#if LOG_MALLOC_INFO
	log_malloc_info();
#endif
}