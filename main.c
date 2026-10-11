/*
 * MIT License
 * Copyright (c) 2025-2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "src/entrance.h"
#include "src/compiler.h"

static void print_usage(FILE* stream) {
	fprintf(stream, "Usage: loxFlux [--print-bytecode] [script]\n");
	fprintf(stream, "  --print-bytecode  dump the compiled bytecode of every function to stderr\n");
	fprintf(stream, "  (with no script, the REPL starts; the flag applies to /eval too)\n");
}

//the main
int main(int argc, C_STR argv[]) {
	C_STR script = NULL;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--print-bytecode") == 0) {
			vm_printBytecode = true;
		}
		else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			print_usage(stdout);
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
