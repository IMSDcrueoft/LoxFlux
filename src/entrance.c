/*
 * MIT License
 * Copyright (c) 2025-2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "entrance.h"
#include "version.h"
#include "vm.h"
#include "file.h"
#include "allocator.h"
#include "compiler.h"

static void print_usage(FILE* stream) {
	fprintf(stream, "Usage: loxFlux [--print-bytecode] [--no-optimize] [script]\n");
	fprintf(stream, "  --print-bytecode  dump the compiled bytecode of every function to stderr\n");
	fprintf(stream, "  --no-optimize     disable all compile-time optimizations (peephole fusion,\n");
	fprintf(stream, "                    constant folding, branch-jump fusion)\n");
	fprintf(stream, "  --version         print the interpreter version and exit\n");
	fprintf(stream, "  (with no script, the REPL starts; flags apply to /eval too)\n");
}

static void print_version() {
	printf("%s %s\n", LOXFLUX_INTERPRETER_NAME, LOXFLUX_INTERPRETER_VERSION);
}

int loxFluxMain(int argc, C_STR argv[]) {
	C_STR script = NULL;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--print-bytecode") == 0) {
			vm_printBytecode = true;
		}
		else if (strcmp(argv[i], "--no-optimize") == 0) {
			vm_optimizationEnabled = false;
		}
		else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			print_usage(stdout);
			return 0;
		}
		else if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
			print_version();
			return 0;
		}
		else if (argv[i][0] == '-' && argv[i][1] != '\0') {
			fprintf(stderr, "Unknown option \"%s\".\n", argv[i]);
			print_usage(stderr);
			exit(64);
		}
		else if (script == NULL) {
			script = argv[i];
		}
		else {
			fprintf(stderr, "Unexpected extra argument \"%s\".\n", argv[i]);
			print_usage(stderr);
			exit(64);
		}
	}

	if (script == NULL) {
		repl();
	}
	else {
		runFile(script);
	}
	return 0;
}

static void print_help() {
	printf("Commands:\n");
	printf("/exit  - Exit the interpreter.\n");
	printf("/eval  - Load file and run.\n");
	printf("/mem   - Print memory statistics.\n");
	printf("/reset - Reset VM state (globals, functions, pools are cleared).\n");
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
		if (*back == '"' || *back == '\n' || *back == '\r') {
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
		while (*back != '\0' && *back != '\n' && *back != '\r') {
			++back;
		}
		*back = '\0';
	}

	back = begin + strlen(begin) - 1;
	while (back >= begin && (*back == ' ' || *back == '\t' || *back == '\r' || *back == '\n')) {
		*back = '\0';
		--back;
	}

	return begin;
}

void repl() {
	printf("%s %s  Copyright (C) %s, %s\n", LOXFLUX_INTERPRETER_NAME, LOXFLUX_INTERPRETER_VERSION, LOXFLUX_INTERPRETER_COPYRIGHT, LOXFLUX_INTERPRETER_DEVELOPER);

	vm_init();

	//512 chars max per line (+ '\n' + '\0'),matches the documented limit
	char line[514];
	STR fullLine = (STR)mem_alloc(512);
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
				else if (match_string(line, "/reset")) {
					size_t reclaimed = vm.bytesAllocated + vm.bytesAllocated_no_gc;
					//full teardown + fresh state: used by the REPL /reset command
					//vm_free runs the DEBUG leak-accounting check, so every /reset doubles as a health check
					vm_free();
					vm_init();
					printf("vm reset: reclaimed %.1f KiB\n", (double)reclaimed / 1024.0);
					continue;
				}
				else if (startsWith_string(line, "/eval ")) {
					STR path = dealWithFilePath(line + 6);
					STR source = readFile(path);
					interpret_repl(source);
					mem_free(source);
					continue;
				}
			}
		}
		else {
			continue;
		}

		if ((fullLineLength + lineLen + 1) >= fullLineCapacity) {
			result = (STR)mem_realloc(fullLine, fullLineCapacity += 1024);
			if (result == NULL) {
				fprintf(stderr, "Memory reallocation failed!\n");
				exit(1);
			}
			fullLine = result;
			result = NULL;
		}

		memcpy(fullLine + fullLineLength, line, lineLen);
		fullLineLength += (lineLen - 1);
		fullLine[fullLineLength] = '\0';

		if (!isContinued) {
#if DEBUG_MODE
			fprintf(stderr, "CODE TEXT:\n%s\n", fullLine);
#endif

			interpret_repl(fullLine);
			fullLine[0] = '\0';
			fullLineLength = 0;
		}
	}

	mem_free(fullLine);

	vm_free();

#if LOG_MALLOC_INFO
	//final stats only: release delayed-free blocks so mimalloc's "current"
	//counters do not look like a leak after a complete teardown
	mem_collect(true);
	log_malloc_info();
#endif
#undef match_string
}

void runFile(C_STR path) {
	vm_init();

	STR source = readFile(path);
	bool ok = interpret_compile(source);
	mem_free(source);

	if (!ok) exit(65);

	InterpretResult result = interpret_execute();

	if (result == INTERPRET_CEDE) {
		fprintf(stderr, "[Cede] execution ceded; no resumer available.\n");
	}
	else if (result == INTERPRET_COMPILE_ERROR) {
		exit(65);
	}
	else if (result == INTERPRET_RUNTIME_ERROR) {
		exit(70);
	}

	vm_free();

#if LOG_MALLOC_INFO
	//final stats only: release delayed-free blocks so mimalloc's "current"
	//counters do not look like a leak after a complete teardown
	mem_collect(true);
	log_malloc_info();
#endif
}