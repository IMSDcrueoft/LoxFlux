/*
 * MIT License
 * Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#define INTERPRETER_VERSION "0.10.0 Dev"
#define INTERPRETER_NAME "LoxFlux"
#define INTERPRETER_COPYRIGHT "2025-2026"
#define INTERPRETER_DEVELOPER "IMSDcrueoft"

/*
* TODO:
*
* TOKEN_FOREACH、TOKEN_IN
*
* foreach(var k,v : arr){ code }
*
* FOREACH IDENTIFIER IDENTIFIER IN EXPRESSION BLOCK
*
* BYTECODE:
* OP_NIL
* OP_NIL
* EXTRESSION...
* OP_MAKE_ITER (check type, push iter or throw error)
* OP_CHECK_ITER (check range, if false pop and jump to break)
* ...
* OP_POPN (pop local vars)
* OP_LOOP
*
* break: OP_POPN (pop local vars and 4slot) OP_JUMP(to end)
* continue: OP_POPN (pop local vars) OP_LOOP(to check iter)
*/