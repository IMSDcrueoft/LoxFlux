//this is the code of cloxed,an enhanced cLox, Dev
#include "src/entrance.h"

//the main
int main(int argc, C_STR argv[]) {
	if (argc == 1) {
		repl();
	}
	else if (argc == 2) {
		runFile(argv[1]);
	}
	else {
		fprintf(stderr, "Usage: [path]\n");
		exit(64);
	}
	return 0;
}