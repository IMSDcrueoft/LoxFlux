/*
 * MIT License
 * Copyright (c) 2025 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "compiler.h"
#include "gc.h"
#include "nativeBuiltin.h"

#if DEBUG_PRINT_CODE
#include "debug.h"
#endif
#if LOG_COMPILE_TIMING
#include "timer.h"
#endif

//shared parser
Parser parser;
Compiler* current = NULL;
ClassCompiler* currentClass = NULL;

static void declaration();
static void expression();
static void statement();
//need to declare first
static ParseRule* getRule(TokenType type);
static void namedVariable(Token name, bool canAssign);

COLD_FUNCTION
static Chunk* currentChunk() {
	return &current->function->chunk;
}

COLD_FUNCTION
static void errorAt(Token* token, C_STR message) {
	//check the panicMode
	if (parser.panicMode) return;
	//set the panicMode
	parser.panicMode = true;

	fprintf(stderr, "[line %d] Error", token->line);

	if (token->type == TOKEN_EOF) {
		fprintf(stderr, " at end");
	}
	else if (token->type == TOKEN_ERROR) {
		// Nothing.
	}
	else {
		fprintf(stderr, " at '%.*s'", token->length, token->start);
	}

	fprintf(stderr, ": %s\n", message);
	parser.hadError = true;
}

COLD_FUNCTION
static void error(C_STR message) {
	errorAt(&parser.previous, message);
}

COLD_FUNCTION
static void errorAtCurrent(C_STR message) {
	errorAt(&parser.current, message);
}

COLD_FUNCTION
static void advance() {
	parser.previous = parser.current;

	while (true) {
		parser.current = scanToken();
		if (parser.current.type != TOKEN_ERROR) break;

		errorAtCurrent(parser.current.start);
	}
}

COLD_FUNCTION
static void consume(TokenType type, C_STR message) {
	if (parser.current.type == type) {
		advance();
		return;
	}

	errorAtCurrent(message);
}

COLD_FUNCTION
static bool check(TokenType type) {
	return parser.current.type == type;
}

COLD_FUNCTION
static bool match(TokenType type) {
	if (!check(type)) return false;
	advance();
	return true;
}

COLD_FUNCTION
static void emitByte(uint8_t byte) {
	chunk_write(currentChunk(), byte, parser.previous.line);
}

//dynamic args
COLD_FUNCTION
static void emitBytes(uint32_t count, uint8_t byte, ...) {
	//write the first
	emitByte(byte);

	// arguments
	va_list arguments;
	va_start(arguments, byte);

	for (uint32_t i = 1; i < count; ++i) {
		byte = va_arg(arguments, int); // int is uint8_t's upper format
		emitByte(byte);
	}

	// clear arguments
	va_end(arguments);
}

COLD_FUNCTION
static void emitConstantCommond(OpCode target, uint32_t index) {
	if (index <= UINT24_MAX) {
		emitBytes(4, target, (uint8_t)index, (uint8_t)(index >> 8), (uint8_t)(index >> 16));
	}
	else {
		error("Too many constants in chunk.");
	}
}

COLD_FUNCTION
static int32_t emitJump(uint8_t instruction) {
	emitByte(instruction);
	emitBytes(2, 0xff, 0xff);
	return currentChunk()->count - 2;
}

// generate loop
COLD_FUNCTION
static void emitLoop(int32_t loopStart) {
	emitByte(OP_LOOP);

	int32_t offset = currentChunk()->count - loopStart + 2;
	if (offset > UINT16_MAX) error("Loop body too large.");

	emitBytes(2, offset & 0xff, (offset >> 8) & 0xff);
}

COLD_FUNCTION
static void emitReturn() {
	if (current->type == TYPE_INITIALIZER) {
		//16bits index get_local
		emitBytes(4, OP_GET_LOCAL, 0, 0, OP_RETURN);
	}
	else {
		emitBytes(2, OP_NIL, OP_RETURN);
	}
}

COLD_FUNCTION
static uint32_t makeConstant(Value value) {
	switch (value.type) {
	case VAL_NUMBER: {
		//deduplicate in pool
		NumberEntry* entry = getNumberEntryInPool(&value);

		if (entry->index == UINT32_MAX) {
			return (entry->index = addConstant(value) & UINT24_MAX);//set value and return
		}
		else {
			return entry->index;
		}
	}
	case VAL_OBJ: {
		switch (OBJ_TYPE(value)) {
		case OBJ_STRING: {
			//find if string is in constant,because it is in pool
			Entry* entry = getStringEntryInPool(AS_STRING(value));

			if (IS_BOOL(entry->value)) {
				uint32_t index = addConstant(value) & UINT24_MAX;
				entry->value.type = VAL_NIL;
				AS_BINARY(entry->value) = AS_BINARY(NIL_VAL) | index;
				return index;
			}
			else {
				return (AS_BINARY(entry->value) & UINT24_MAX);
			}
			break;
		}
		default:
			return addConstant(value);
		}
		break;
	}
	default://object type
		return addConstant(value);
	}
}

COLD_FUNCTION
static void emitConstant(Value value) {
	emitConstantCommond(OP_CONSTANT, makeConstant(value));
}

COLD_FUNCTION
static void patchJump(int32_t offset) {
	// -2 to adjust for the bytecode for the jump offset itself.
	int32_t jump = currentChunk()->count - offset - 2;

	if (jump > UINT16_MAX) {
		error("Too much code to jump over.");
	}

	//write low and high
	currentChunk()->code[offset] = jump & 0xff;
	currentChunk()->code[offset + 1] = (jump >> 8) & 0xff;
}

COLD_FUNCTION
static void initCompiler(Compiler* compiler, FunctionType type) {
	compiler->enclosing = current;
	current = compiler;//must set now

	//init
	compiler->currentLoop = NULL;

	compiler->function = NULL;
	compiler->type = type;

	compiler->localCount = 0;
	compiler->scopeDepth = 0;
	//init with 1024 slots
	compiler->locals = ALLOCATE_NO_GC(Local, LOCAL_INIT);
	compiler->localCapacity = LOCAL_INIT;

	compiler->function = newFunction();
	compiler->objectNestingDepth = 0;

	//it's a function
	switch (type) {
	case TYPE_FUNCTION:
		compiler->function->name = copyString(parser.previous.start, parser.previous.length, false);
		break;
	case TYPE_LAMBDA:
		compiler->function->name = copyString("", 0, false);
		break;
	default:
		break;
	}

	Local* local = &compiler->locals[compiler->localCount++];
	local->depth = 0;
	local->isCaptured = false;

	if (type != TYPE_FUNCTION && type != TYPE_LAMBDA) {
		local->name.start = "this";
		local->name.length = 4;
	}
	else {
		local->name.start = "";
		local->name.length = 0;
	}

	if (compiler->enclosing == NULL) {
		compiler->nestingDepth = 0;
	}
	else {
		compiler->nestingDepth = compiler->enclosing->nestingDepth + 1;
		if (compiler->nestingDepth == FUNCTION_MAX_NESTING) {
			error("Too many nested functions.");
		}
	}
}

COLD_FUNCTION
static void freeLocals(Compiler* compiler) {
	FREE_ARRAY_NO_GC(Local, compiler->locals, compiler->localCapacity);
	compiler->locals = NULL;
	compiler->localCapacity = 0;
}

COLD_FUNCTION
static ObjFunction* endCompiler() {
	emitReturn();

	ObjFunction* function = current->function;
#if DEBUG_PRINT_CODE
	if (!parser.hadError) {
		disassembleChunk(currentChunk(), (function->name != NULL)
			? ((function->name->length != 0) ? function->name->chars : "<lambda>") : "<script>", function->id);
	}
#endif
	current = current->enclosing;
	return function;
}

COLD_FUNCTION
static void beginScope() {
	current->scopeDepth++;
}

COLD_FUNCTION
static void emitPopCount(uint16_t popCount) {
	if (popCount == 0) return;
	if (popCount > 1) { // 16-bit index
		emitBytes(3, OP_POP_N, (uint8_t)popCount, (uint8_t)(popCount >> 8));
	}
	else {
		emitByte(OP_POP);
	}
}

COLD_FUNCTION
static void endScope() {
	current->scopeDepth--;

	uint32_t popCount = 0;

	while ((current->localCount > 0) && (current->locals[current->localCount - 1].depth > current->scopeDepth)) {
		if (current->locals[current->localCount - 1].isCaptured) {
			if (popCount > 0) {
				emitPopCount(popCount);
				popCount = 0;
			}

			emitByte(OP_CLOSE_UPVALUE);
		}
		else {
			++popCount;
		}
		current->localCount--;
	}

	if (popCount > 0) {
		emitPopCount(popCount);
	}
}

COLD_FUNCTION
static void parsePrecedence(Precedence precedence) {
	advance();

	ParseFn prefixRule = getRule(parser.previous.type)->prefix;

	if (prefixRule == NULL) {
		error("Expect expression.");
		return;
	}

	bool canAssign = precedence <= PREC_ASSIGNMENT;
	prefixRule(canAssign);

	while (precedence <= getRule(parser.current.type)->precedence) {
		advance();
		ParseFn infixRule = getRule(parser.previous.type)->infix;

		if (infixRule == NULL) {
			error("Syntax error, no infix syntax at current location.");
			break;
		}

		infixRule(canAssign);
	}

	if (canAssign && match(TOKEN_EQUAL)) {
		error("Invalid assignment target.");
	}
}

COLD_FUNCTION
static uint32_t identifierConstant(Token* name) {
	return makeConstant(OBJ_VAL(copyString(name->start, name->length, false)));
}

COLD_FUNCTION
static void addLocal(Token name) {
	if (current->localCount == LOCAL_MAX) {
		error("Too many nested local variables in scope.");
		return;
	}

	if (current->localCount == current->localCapacity) {
		//grow
		uint32_t oldCapacity = current->localCapacity;
		current->localCapacity = GROW_CAPACITY(oldCapacity);
		current->locals = GROW_ARRAY_NO_GC(Local, current->locals, oldCapacity, current->localCapacity);
	}

	Local* local = &current->locals[current->localCount++];
	local->name = name;
	local->depth = -1;// var a = a;??? avoid this
	local->isCaptured = false;
	local->isConst = false;
}

COLD_FUNCTION
static bool identifiersEqual(Token* a, Token* b) {
	if (a->length != b->length) return false;
	return memcmp(a->start, b->start, a->length) == 0;
}

COLD_FUNCTION
static void declareVariable() {
	//it is global
	if (current->scopeDepth == 0) return;

	Token* name = &parser.previous;

	for (int32_t i = current->localCount - 1; i >= 0; i--) {
		Local* local = &current->locals[i];

		if (local->depth != -1 && local->depth < current->scopeDepth) {
			break;
		}

		if (identifiersEqual(name, &local->name)) {
			error("Already a variable with this name in this scope.");
		}
	}

	addLocal(*name);
}

typedef struct {
	int32_t arg;
	bool isConst;
} LocalInfo;

COLD_FUNCTION
static LocalInfo resolveLocal(Compiler* compiler, Token* name) {
	for (int32_t i = compiler->localCount - 1; i >= 0; i--) {
		Local* local = &compiler->locals[i];
		if (identifiersEqual(name, &local->name)) {
			if (local->depth == -1) {
				error("Can't read local variable in its own initializer.");
			}

			return (LocalInfo) { .arg = i, .isConst = local->isConst };
		}
	}

	//it is a global declared var
	return (LocalInfo) { .arg = -1, .isConst = false };
}

COLD_FUNCTION
static uint32_t addUpvalue(Compiler* compiler, int32_t index, bool isLocal) {
	uint32_t upvalueCount = compiler->function->upvalueCount;

	//deduplicate
	for (uint32_t i = 0; i < upvalueCount; i++) {
		Upvalue* upvalue = &compiler->upvalues[i];
		if (upvalue->index == index && upvalue->isLocal == isLocal) {
			return i;
		}
	}

	if (upvalueCount == UINT8_COUNT) {
		error("Too many closure variables in function.");
		return 0;
	}

	compiler->upvalues[upvalueCount].isLocal = isLocal;
	compiler->upvalues[upvalueCount].index = index;
	return compiler->function->upvalueCount++;
}

COLD_FUNCTION
static LocalInfo resolveUpvalue(Compiler* compiler, Token* name) {
	if (compiler->enclosing == NULL) return (LocalInfo) { .arg = -1, .isConst = false };

	//local
	LocalInfo localInfo = resolveLocal(compiler->enclosing, name);
	if (localInfo.arg != -1) {
		compiler->enclosing->locals[localInfo.arg].isCaptured = true;//mark it as captured
		localInfo.arg = addUpvalue(compiler, localInfo.arg, true);
		return localInfo;
	}

	//recursion
	LocalInfo upvalue = resolveUpvalue(compiler->enclosing, name);
	if (upvalue.arg != -1) {
		upvalue.arg = addUpvalue(compiler, upvalue.arg, false);
		return upvalue;
	}

	return (LocalInfo) { .arg = -1, .isConst = false };
}

COLD_FUNCTION
static uint32_t parseVariable(C_STR errorMessage) {
	consume(TOKEN_IDENTIFIER, errorMessage);

	declareVariable();
	//it is a local one
	if (current->scopeDepth > 0) return 0;

	return identifierConstant(&parser.previous);
}

COLD_FUNCTION
static void markInitialized(bool isConst) {
	if (current->scopeDepth == 0) return;
	//only defined local vars could be used
	current->locals[current->localCount - 1].depth = current->scopeDepth;
	current->locals[current->localCount - 1].isConst = isConst;
}

COLD_FUNCTION
static void defineVariable(uint32_t global) {
	//it is a local one
	if (current->scopeDepth > 0) {
		markInitialized(false);
		return;
	}

	emitConstantCommond(OP_DEFINE_GLOBAL, global);
}

COLD_FUNCTION
static void defineConst(uint32_t global) {
	//it is a local one
	if (current->scopeDepth > 0) {
		markInitialized(true);
		return;
	}
	else {
		errorAtCurrent("Constant can only be defined in the local scope.");
	}
}

COLD_FUNCTION
static uint8_t argumentList() {
	uint8_t argCount = 0;
	if (!check(TOKEN_RIGHT_PAREN)) {
		do {
			expression();

			if (argCount == 255) {
				error("Can't have more than 255 arguments.");
			}

			argCount++;
		} while (match(TOKEN_COMMA));
	}
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
	return argCount;
}

COLD_FUNCTION
static void and_(bool canAssign) {
	int32_t endJump = emitJump(OP_JUMP_IF_FALSE);

	emitByte(OP_POP);
	parsePrecedence(PREC_AND);

	patchJump(endJump);
}

COLD_FUNCTION
static void expression() {
	parsePrecedence(PREC_ASSIGNMENT);
}

COLD_FUNCTION
static void block() {
	while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
		declaration();
	}

	consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

COLD_FUNCTION
static void function(FunctionType type) {
	Compiler compiler;
	initCompiler(&compiler, type);
	beginScope();

	if (type == TYPE_FUNCTION) {
		consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");
	}
	else {
		consume(TOKEN_LEFT_PAREN, "Expect '(' after lambda.");
	}

	if (!check(TOKEN_RIGHT_PAREN)) {
		do {
			current->function->arity++;
			if (current->function->arity > 255) {
				errorAtCurrent("Can't have more than 255 parameters.");
			}

			uint32_t constant = parseVariable("Expect parameter name.");
			defineVariable(constant);

		} while (match(TOKEN_COMMA));
	}

	consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
	consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
	block();

	ObjFunction* function = endCompiler();

	//create a closure
	emitConstantCommond(OP_CLOSURE, makeConstant(OBJ_VAL(function)));

	//insert upValue index
	for (uint32_t i = 0; i < function->upvalueCount; i++) {
		emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
		emitBytes(2, (uint8_t)(compiler.upvalues[i].index), (uint8_t)(compiler.upvalues[i].index >> 8));
	}

	freeLocals(&compiler);
}

COLD_FUNCTION
static void lambda(bool canAssign) {
	FunctionType type = TYPE_LAMBDA;
	function(type);
}

COLD_FUNCTION
static void method() {
	consume(TOKEN_IDENTIFIER, "Expect method name.");
	uint32_t constant = identifierConstant(&parser.previous);

	FunctionType type = TYPE_METHOD;
	if (parser.previous.length == 4 && memcmp(parser.previous.start, "init", 4) == 0) {
		type = TYPE_INITIALIZER;
	}
	function(type);
	emitConstantCommond(OP_METHOD, constant);
}

COLD_FUNCTION
static void classDeclaration() {
	consume(TOKEN_IDENTIFIER, "Expect class name.");
	Token className = parser.previous;

	uint32_t nameConstant = identifierConstant(&parser.previous);
	declareVariable();

	emitConstantCommond(OP_CLASS, nameConstant);
	defineVariable(nameConstant);

	//link the chain
	ClassCompiler classCompiler = { .enclosing = currentClass };
	currentClass = &classCompiler;

	//put the class back to stack
	namedVariable(className, false);
	consume(TOKEN_LEFT_BRACE, "Expect '{' before class body.");
	while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
		method();
	}
	consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body.");
	//pop out the class
	emitByte(OP_POP);

	//resume
	currentClass = currentClass->enclosing;
}

COLD_FUNCTION
static void funDeclaration() {
	uint32_t arg = parseVariable("Expect function name.");
	markInitialized(false);
	function(TYPE_FUNCTION);
	defineVariable(arg);
}

COLD_FUNCTION
static void varDeclaration() {
	do {
		uint32_t arg = parseVariable("Expect variable name.");

		if (match(TOKEN_EQUAL)) {
			expression();
		}
		else {
			emitByte(OP_NIL);
		}

		defineVariable(arg);

		if (parser.hadError) return;
	} while (match(TOKEN_COMMA));

	consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
}

COLD_FUNCTION
static void constDeclaration() {
	do {
		uint32_t arg = parseVariable("Expect constant name.");

		if (!match(TOKEN_EQUAL)) {
			errorAtCurrent("Constant must be initialized.");
		}

		expression();

		defineConst(arg);

		if (parser.hadError) return;
	} while (match(TOKEN_COMMA));

	consume(TOKEN_SEMICOLON, "Expect ';' after constant declaration.");
}

COLD_FUNCTION
static void expressionStatement() {
	expression();
	consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
	emitByte(OP_POP);
}

COLD_FUNCTION
static void forStatement() {
	//for is a block
	beginScope();

	consume(TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

	if (match(TOKEN_SEMICOLON)) { //like this -> for(;;)
		// No initializer.
	}
	else if (match(TOKEN_VAR)) { //like this -> for(var i = 0;;)
		varDeclaration();
	}
	else { //like this -> for( here ;;)
		expressionStatement();
	}

	int32_t loopStart = currentChunk()->count;

	int32_t exitJump = -1;
	if (!match(TOKEN_SEMICOLON)) {//for(; here ;)
		expression();
		consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

		// Jump out of the loop if the condition is false.
		exitJump = emitJump(OP_JUMP_IF_FALSE_POP);
	}

	//the code is: init,condition,increase,body,loop_to_increase
	if (!match(TOKEN_RIGHT_PAREN)) { // for(;;here)
		//we don't do increase first,we go to body first
		int32_t bodyJump = emitJump(OP_JUMP);
		int32_t incrementStart = currentChunk()->count;
		expression();
		emitByte(OP_POP);

		consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

		//we go to where the condition is
		emitLoop(loopStart);
		//after the body,we go to increase
		loopStart = incrementStart;
		//patch the body,first time,we jump to body
		patchJump(bodyJump);
	}

	//record the loop
	LoopContext loop = (LoopContext){ .start = loopStart, .enclosing = current->currentLoop,.breakJumps = NULL,.breakJumpCount = 0 ,.enterParamCount = current->localCount };
	loop.breakJumpCapacity = 8;
	loop.breakJumps = ALLOCATE_NO_GC(int32_t, loop.breakJumpCapacity);
	current->currentLoop = &loop;

	statement();
	emitLoop(loopStart);

	//if there is no exitJump,this is an infinite loop
	if (exitJump != -1) {
		patchJump(exitJump);
	}

	while (loop.breakJumpCount > 0) {
		patchJump(loop.breakJumps[--loop.breakJumpCount]);
	}

	FREE_ARRAY_NO_GC(int32_t, loop.breakJumps, loop.breakJumpCapacity);
	current->currentLoop = current->currentLoop->enclosing;
	//end block
	endScope();
}

COLD_FUNCTION
static void ifStatement() {
	consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

	int32_t thenJump = emitJump(OP_JUMP_IF_FALSE_POP);
	statement();

	int32_t elseJump = emitJump(OP_JUMP);
	patchJump(thenJump);

	if (match(TOKEN_ELSE)) statement();
	patchJump(elseJump);
}

COLD_FUNCTION
static void branchCaseStatement() {
	if (!match(TOKEN_NONE)) {
		expression();
		int32_t thenJump = emitJump(OP_JUMP_IF_FALSE_POP);
		consume(TOKEN_COLON, "Expect ':' after condition.");
		statement();

		int32_t elseJump = emitJump(OP_JUMP);
		patchJump(thenJump);

		//prevent endless stack overflow
		if (parser.hadError) return;

		//seek '}' or next case
		if (!match(TOKEN_RIGHT_BRACE)) {
			branchCaseStatement();
		}
		patchJump(elseJump);
	}
	else {
		consume(TOKEN_COLON, "Expect ':' after 'none'.");
		statement();
		//'none' must be the last case
		consume(TOKEN_RIGHT_BRACE, "Expect '}' after 'none' case.");
	}
}

COLD_FUNCTION
static void branchStatement() {
	consume(TOKEN_LEFT_BRACE, "Expect '{' after 'match'.");
	branchCaseStatement();
}

COLD_FUNCTION
static void printStatement() {
	expression();
	consume(TOKEN_SEMICOLON, "Expect ';' after value.");
	emitByte(OP_PRINT);
}

COLD_FUNCTION
static void throwStatement() {
	expression();
	consume(TOKEN_SEMICOLON, "Expect ';' after value.");
	emitByte(OP_THROW);
}

COLD_FUNCTION
static void returnStatement() {
	if (current->type == TYPE_SCRIPT) {
		error("Can't return from top-level code.");
	}

	if (match(TOKEN_SEMICOLON)) {
		emitReturn();
	}
	else {
		if (current->type == TYPE_INITIALIZER) {
			error("Can't return a value from an initializer.");
		}

		expression();
		consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
		emitByte(OP_RETURN);
	}
}

COLD_FUNCTION
static void whileStatement() {
	int32_t loopStart = currentChunk()->count;

	consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

	int32_t exitJump = emitJump(OP_JUMP_IF_FALSE_POP);

	//record the loop
	LoopContext loop = (LoopContext){ .start = loopStart, .enclosing = current->currentLoop,.breakJumps = NULL,.breakJumpCount = 0 ,.enterParamCount = current->localCount };
	loop.breakJumpCapacity = 8;
	loop.breakJumps = ALLOCATE_NO_GC(int32_t, loop.breakJumpCapacity);
	current->currentLoop = &loop;

	statement();

	emitLoop(loopStart);

	patchJump(exitJump);

	while (loop.breakJumpCount > 0) {
		patchJump(loop.breakJumps[--loop.breakJumpCount]);
	}

	FREE_ARRAY(int32_t, loop.breakJumps, loop.breakJumpCapacity);
	current->currentLoop = current->currentLoop->enclosing;
}

//only the loop itself can fix the jump position
COLD_FUNCTION
static void breakStatement() {
	if (current->currentLoop == NULL) {
		error("Cannot use 'break' outside of a loop.");
		return;
	}

	uint16_t offsetParam = current->localCount - current->currentLoop->enterParamCount;
	emitPopCount(offsetParam);
	int32_t jump = emitJump(OP_JUMP);

	if (current->currentLoop->breakJumpCount == current->currentLoop->breakJumpCapacity) {
		if (current->currentLoop->breakJumpCapacity == UINT16_MAX) {
			error("Too many break statements in one loop.");
			return;
		}

		uint32_t oldCapacity = current->currentLoop->breakJumpCapacity;
		current->currentLoop->breakJumpCapacity = GROW_CAPACITY(oldCapacity);
		current->currentLoop->breakJumps = GROW_ARRAY_NO_GC(int32_t, current->currentLoop->breakJumps, oldCapacity, current->currentLoop->breakJumpCapacity);
	}

	current->currentLoop->breakJumps[current->currentLoop->breakJumpCount++] = jump;

	consume(TOKEN_SEMICOLON, "Expect ';' after 'break'.");
}

COLD_FUNCTION
static void continueStatement() {
	if (current->currentLoop == NULL) {
		error("Cannot use 'continue' outside of a loop.");
		return;
	}

	uint16_t offsetParam = current->localCount - current->currentLoop->enterParamCount;
	emitPopCount(offsetParam);
	emitLoop(current->currentLoop->start);
	consume(TOKEN_SEMICOLON, "Expect ';' after 'continue'.");
}

COLD_FUNCTION
static void synchronize() {
	parser.panicMode = false;

	while (parser.current.type != TOKEN_EOF) {
		if (parser.previous.type == TOKEN_SEMICOLON) return;
		switch (parser.current.type) {
		case TOKEN_CLASS:
		case TOKEN_FUN:
		case TOKEN_VAR:
		case TOKEN_CONST:
		case TOKEN_FOR:
		case TOKEN_IF:
		case TOKEN_BRANCH:
		case TOKEN_WHILE:
		case TOKEN_PRINT:
		case TOKEN_RETURN:
		case TOKEN_THROW:
			return;

		default:
			break; // Do nothing.
		}

		advance();
	}
}

COLD_FUNCTION
static void statement() {
	if (match(TOKEN_PRINT)) {
		printStatement();
	}
	else if (match(TOKEN_IF)) {
		ifStatement();
	}
	else if (match(TOKEN_BRANCH)) {
		branchStatement();
	}
	else if (match(TOKEN_RETURN)) {
		returnStatement();
	}
	else if (match(TOKEN_WHILE)) {
		whileStatement();
	}
	else if (match(TOKEN_FOR)) {
		forStatement();
	}
	else if (match(TOKEN_BREAK)) {
		breakStatement();
	}
	else if (match(TOKEN_CONTINUE)) {
		continueStatement();
	}
	else if (match(TOKEN_LEFT_BRACE)) {
		beginScope();
		block();
		endScope();
	}
	else if (match(TOKEN_THROW)) {
		throwStatement();
	}
	else {
		expressionStatement();
	}
}

COLD_FUNCTION
static void declaration() {
	//might went panicMode
	if (parser.panicMode) synchronize();

	if (match(TOKEN_CLASS)) {
		classDeclaration();
	}
	else if (match(TOKEN_FUN)) {
		funDeclaration();
	}
	else if (match(TOKEN_VAR)) {
		varDeclaration();
	}
	else if (match(TOKEN_CONST)) {
		constDeclaration();
	}
	else {
		statement();
	}

	//If we hit a compile error while parsing the previous statement, we enter panic mode. When that happens, after the statement we start synchronizing.
	if (parser.panicMode) synchronize();
}

COLD_FUNCTION
static void binary(bool canAssign) {
	TokenType operatorType = parser.previous.type;
	ParseRule* rule = getRule(operatorType);
	parsePrecedence((Precedence)(rule->precedence + 1));

	switch (operatorType) {
	case TOKEN_PLUS: emitByte(OP_ADD); break;
	case TOKEN_MINUS: emitByte(OP_SUBTRACT); break;
	case TOKEN_STAR: emitByte(OP_MULTIPLY); break;
	case TOKEN_SLASH: emitByte(OP_DIVIDE); break;
	case TOKEN_PERCENT: emitByte(OP_MODULUS); break;
	case TOKEN_BANG_EQUAL: emitByte(OP_NOT_EQUAL); break;
	case TOKEN_EQUAL_EQUAL: emitByte(OP_EQUAL); break;
	case TOKEN_GREATER: emitByte(OP_GREATER); break;
	case TOKEN_GREATER_EQUAL: emitByte(OP_GREATER_EQUAL); break;
	case TOKEN_LESS: emitByte(OP_LESS); break;
	case TOKEN_LESS_EQUAL: emitByte(OP_LESS_EQUAL); break;
	case TOKEN_INSTANCE_OF: emitByte(OP_INSTANCE_OF); break;
	case TOKEN_BIT_AND: emitBytes(2, OP_BITWISE, BIT_OP_AND); break;
	case TOKEN_BIT_OR: emitBytes(2, OP_BITWISE, BIT_OP_OR); break;
	case TOKEN_BIT_XOR: emitBytes(2, OP_BITWISE, BIT_OP_XOR); break;
	case TOKEN_BIT_SHL: emitBytes(2, OP_BITWISE, BIT_OP_SHL); break;
	case TOKEN_BIT_SHR: emitBytes(2, OP_BITWISE, BIT_OP_SHR); break;
	case TOKEN_BIT_SAR: emitBytes(2, OP_BITWISE, BIT_OP_SAR); break;
	default: return; // Unreachable.
	}
}

COLD_FUNCTION
static void call(bool canAssign) {
	uint8_t argCount = argumentList();
	emitBytes(2, OP_CALL, argCount);
}

COLD_FUNCTION
static void dot(bool canAssign) {
	consume(TOKEN_IDENTIFIER, "Expect property name after '.'.");
	uint32_t name = identifierConstant(&parser.previous);

	if (canAssign && match(TOKEN_EQUAL)) {
		expression();
		emitConstantCommond(OP_SET_PROPERTY, name);
	}
	else if (match(TOKEN_LEFT_PAREN)) {
		uint8_t argCount = argumentList();
		emitConstantCommond(OP_INVOKE, name);
		emitByte(argCount);
	}
	else {
		emitConstantCommond(OP_GET_PROPERTY, name);
	}
}

COLD_FUNCTION
static void arrayLiteral(bool canAssign) {
	uint32_t elementCount = 0;
	if (!check(TOKEN_RIGHT_SQUARE_BRACKET) && !check(TOKEN_EOF)) {
		do {
			expression(); //parse values
			elementCount++;

			if (parser.hadError) return;
		} while (match(TOKEN_COMMA));
	}
	consume(TOKEN_RIGHT_SQUARE_BRACKET, "Expect ']' to close the array.");

	if (elementCount > ARRAY_MAX) {
		error("Array literal is too long.");
		return;
	}

	emitBytes(3, OP_NEW_ARRAY, (uint8_t)elementCount, (uint8_t)(elementCount >> 8));  //make array
}

COLD_FUNCTION
static void objectLiteral(bool canAssign) {
	if (current->objectNestingDepth == OBJECT_MAX_NESTING) {
		error("Too many nested objects.");
		return;
	}

	++current->objectNestingDepth;
	emitByte(OP_NEW_OBJECT);

	if (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
		do {
			uint32_t constant = UINT32_MAX;//limit is 2^24-1

			if (match(TOKEN_IDENTIFIER)) {
				constant = identifierConstant(&parser.previous);
			}
			else if (match(TOKEN_STRING)) {
				constant = makeConstant(OBJ_VAL(copyString(parser.previous.start + 1, parser.previous.length - 2, false)));
			}
			else if (match(TOKEN_STRING_ESCAPE)) {
				constant = makeConstant(OBJ_VAL(copyString(parser.previous.start + 1, parser.previous.length - 2, true)));
			}
			else {
				errorAtCurrent("Expect property name.");
			}

			consume(TOKEN_COLON, "Expect ':' after property name.");
			expression(); //get value
			emitConstantCommond(OP_NEW_PROPERTY, constant);

			if (parser.hadError) {
				--current->objectNestingDepth;
				return;
			}
		} while (match(TOKEN_COMMA));
	}

	consume(TOKEN_RIGHT_BRACE, "Expect '}' to close the object.");
	--current->objectNestingDepth;
}

COLD_FUNCTION
static void subscript(bool canAssign) {
	expression();
	consume(TOKEN_RIGHT_SQUARE_BRACKET, "Expect ']' after subscript.");
	if (canAssign && match(TOKEN_EQUAL)) {
		expression(); // parse assignment
		emitByte(OP_SET_SUBSCRIPT);
	}
	else {
		emitByte(OP_GET_SUBSCRIPT);
	}
}

//check builtin
COLD_FUNCTION
static void builtinLiteral(bool canAssign) {
	switch (parser.previous.type)
	{
	case TOKEN_MODULE_GLOBAL:emitByte(OP_MODULE_GLOBAL); break;
	case TOKEN_MODULE_MATH:emitBytes(2, OP_MODULE_BUILTIN, MODULE_MATH); break;
	case TOKEN_MODULE_ARRAY:emitBytes(2, OP_MODULE_BUILTIN, MODULE_ARRAY); break;
	case TOKEN_MODULE_OBJECT:emitBytes(2, OP_MODULE_BUILTIN, MODULE_OBJECT); break;
	case TOKEN_MODULE_STRING:emitBytes(2, OP_MODULE_BUILTIN, MODULE_STRING); break;
	case TOKEN_MODULE_TIME:emitBytes(2, OP_MODULE_BUILTIN, MODULE_TIME); break;
	case TOKEN_MODULE_FILE:emitBytes(2, OP_MODULE_BUILTIN, MODULE_FILE); break;
	case TOKEN_MODULE_SYSTEM:emitBytes(2, OP_MODULE_BUILTIN, MODULE_SYSTEM); break;
	default:emitByte(OP_NIL); break;
	}
}

COLD_FUNCTION
static void literal(bool canAssign) {
	switch (parser.previous.type) {
	case TOKEN_FALSE: emitByte(OP_FALSE); break;
	case TOKEN_NIL: emitByte(OP_NIL); break;
	case TOKEN_TRUE: emitByte(OP_TRUE); break;
	default: return; // Unreachable.
	}
}

COLD_FUNCTION
static void grouping(bool canAssign) {
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

COLD_FUNCTION
static void emitNumber(double value) {
	emitConstant(NUMBER_VAL(value));
}

COLD_FUNCTION
static void number(bool canAssign) {
	emitNumber(strtod(parser.previous.start, NULL));
}

COLD_FUNCTION
static void number_bin(bool canAssign) {
	emitNumber(strtol(parser.previous.start + 2, NULL, 2));
}

COLD_FUNCTION
static void number_hex(bool canAssign) {
	emitNumber(strtol(parser.previous.start + 2, NULL, 16));
}

COLD_FUNCTION
static void or_(bool canAssign) {
	//if false
	//int32_t elseJump = emitJump(OP_JUMP_IF_FALSE);
	//int32_t endJump = emitJump(OP_JUMP);

	//patchJump(elseJump);
	//emitByte(OP_POP);

	//parsePrecedence(PREC_OR);
	//patchJump(endJump);

	//if true
	int32_t ifJump = emitJump(OP_JUMP_IF_TRUE);
	emitByte(OP_POP);
	parsePrecedence(PREC_OR);
	patchJump(ifJump);
}

COLD_FUNCTION
static void string(bool canAssign) {
	emitConstant(OBJ_VAL(copyString(parser.previous.start + 1, parser.previous.length - 2, false)));
}

COLD_FUNCTION
static void namedVariable(Token name, bool canAssign) {
	LocalInfo args = resolveLocal(current, &name);
	int32_t arg = args.arg;

	if (arg != -1) {//it's a local var

		if (canAssign && match(TOKEN_EQUAL)) {
			expression();

			if (args.isConst) {
				errorAtCurrent("Assignment to constant variable.");
				return;
			}
			// 16-bit index
			emitBytes(3, OP_SET_LOCAL, (uint8_t)arg, (uint8_t)(arg >> 8));
		}
		else { // 16-bit index
			emitBytes(3, OP_GET_LOCAL, (uint8_t)arg, (uint8_t)(arg >> 8));
		}
	}
	else {
		args = resolveUpvalue(current, &name);
		arg = args.arg;

		if (arg != -1) {//it's an upvalue
			if (canAssign && match(TOKEN_EQUAL)) {
				expression();

				if (args.isConst) {
					errorAtCurrent("Assignment to constant variable.");
					return;
				}
				// 16-bit index
				emitBytes(2, OP_SET_UPVALUE, (uint8_t)arg);
			}
			else { // 16-bit index
				emitBytes(2, OP_GET_UPVALUE, (uint8_t)arg);
			}
		}
		else {//it's a global var
			arg = identifierConstant(&name);

			if (canAssign && match(TOKEN_EQUAL)) {
				expression();
				emitConstantCommond(OP_SET_GLOBAL, arg);
			}
			else {
				emitConstantCommond(OP_GET_GLOBAL, arg);
			}
		}
	}
}

COLD_FUNCTION
static void variable(bool canAssign) {
	namedVariable(parser.previous, canAssign);
}

COLD_FUNCTION
static void this_(bool canAssign) {
	if (currentClass == NULL) {
		error("Can't use 'this' outside of a class.");
		return;
	}

	variable(false);
}

COLD_FUNCTION
static void string_escape(bool canAssign) {
	emitConstant(OBJ_VAL(copyString(parser.previous.start + 1, parser.previous.length - 2, true)));
}

COLD_FUNCTION
static void unary(bool canAssign) {
	TokenType operatorType = parser.previous.type;

	// Compile the operand.
	parsePrecedence(PREC_UNARY);

	// Emit the operator instruction.
	switch (operatorType) {
	case TOKEN_BANG: emitByte(OP_NOT); break;
	case TOKEN_MINUS: emitByte(OP_NEGATE); break;
	case TOKEN_BIT_NOT: emitBytes(2, OP_BITWISE, BIT_OP_NOT); break;
	case TOKEN_TYPE_OF: emitByte(OP_TYPE_OF); break;
	default: return; // Unreachable.
	}
}

ParseRule rules[] = {
	[TOKEN_LEFT_PAREN] = {grouping, call,   PREC_CALL},
	[TOKEN_RIGHT_PAREN] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_LEFT_BRACE] = {objectLiteral,     NULL,   PREC_CALL},
	[TOKEN_RIGHT_BRACE] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_LEFT_SQUARE_BRACKET] = {arrayLiteral,	subscript,	PREC_CALL},
	[TOKEN_RIGHT_SQUARE_BRACKET] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_COMMA] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_DOT] = {NULL,     dot,   PREC_CALL},
	[TOKEN_MINUS] = {unary   ,    binary, PREC_TERM     },
	[TOKEN_PLUS] = {NULL,     binary, PREC_TERM    },
	[TOKEN_SEMICOLON] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_COLON] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_SLASH] = {NULL,     binary, PREC_FACTOR  },
	[TOKEN_STAR] = {NULL,     binary, PREC_FACTOR  },
	[TOKEN_PERCENT] = {NULL,     binary, PREC_FACTOR  },
	[TOKEN_BANG] = {unary,     NULL,   PREC_NONE},
	[TOKEN_BANG_EQUAL] = {NULL,     binary,   PREC_EQUALITY},
	[TOKEN_EQUAL] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_EQUAL_EQUAL] = {NULL,     binary, PREC_EQUALITY},
	[TOKEN_GREATER] = {NULL,     binary, PREC_COMPARISON},
	[TOKEN_GREATER_EQUAL] = {NULL,     binary, PREC_COMPARISON},
	[TOKEN_LESS] = {NULL,     binary, PREC_COMPARISON},
	[TOKEN_LESS_EQUAL] = {NULL,     binary, PREC_COMPARISON},
	[TOKEN_INSTANCE_OF] = {NULL,     binary, PREC_INSTANCEOF},
	[TOKEN_TYPE_OF] = {unary,	NULL, PREC_UNARY},
	[TOKEN_BIT_AND] = {NULL,	binary, PREC_BITWISE},
	[TOKEN_BIT_OR] = {NULL,		binary, PREC_BITWISE},
	[TOKEN_BIT_XOR] = {NULL,	binary, PREC_BITWISE},
	[TOKEN_BIT_NOT] = {unary,	NULL, PREC_UNARY},
	[TOKEN_BIT_SHL] = {NULL,	binary, PREC_BITWISE},
	[TOKEN_BIT_SHR] = {NULL,	binary, PREC_BITWISE},
	[TOKEN_BIT_SAR] = {NULL,	binary, PREC_BITWISE},
	[TOKEN_IDENTIFIER] = {variable,     NULL,   PREC_NONE},
	[TOKEN_STRING] = {string,     NULL,   PREC_NONE},
	[TOKEN_STRING_ESCAPE] = {string_escape,     NULL,   PREC_NONE},
	[TOKEN_NUMBER] = {number  ,   NULL,   PREC_NONE  },
	[TOKEN_NUMBER_BIN] = {number_bin  ,   NULL,   PREC_NONE  },
	[TOKEN_NUMBER_HEX] = {number_hex  ,   NULL,   PREC_NONE  },
	[TOKEN_MODULE_GLOBAL] = {builtinLiteral,     NULL,   PREC_NONE},
	[TOKEN_MODULE_MATH] = {builtinLiteral,     NULL,   PREC_NONE},
	[TOKEN_MODULE_ARRAY] = {builtinLiteral,     NULL,   PREC_NONE},
	[TOKEN_MODULE_OBJECT] = {builtinLiteral,     NULL,   PREC_NONE},
	[TOKEN_MODULE_STRING] = {builtinLiteral,     NULL,   PREC_NONE},
	[TOKEN_MODULE_TIME] = {builtinLiteral,     NULL,   PREC_NONE},
	[TOKEN_MODULE_FILE] = {builtinLiteral,     NULL,   PREC_NONE},
	[TOKEN_MODULE_SYSTEM] = {builtinLiteral,     NULL,   PREC_NONE},
	[TOKEN_AND] = {NULL,     and_,   PREC_AND},
	[TOKEN_CLASS] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_ELSE] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_FALSE] = {literal,     NULL,   PREC_NONE},
	[TOKEN_FOR] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_FUN] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_LAMBDA] = {lambda,	NULL,	PREC_NONE},
	[TOKEN_IF] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_BRANCH] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_NONE] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_NIL] = {literal,     NULL,   PREC_NONE},
	[TOKEN_OR] = {NULL,     or_,   PREC_OR},
	[TOKEN_PRINT] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_THROW] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_RETURN] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_SUPER] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_THIS] = {this_,     NULL,   PREC_NONE},
	[TOKEN_TRUE] = {literal,     NULL,   PREC_NONE},
	[TOKEN_VAR] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_CONST] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_WHILE] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_ERROR] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_EOF] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_BREAK] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_CONTINUE] = {NULL,     NULL,   PREC_NONE},
};

COLD_FUNCTION
static ParseRule* getRule(TokenType type) {
	return &rules[type];
}

COLD_FUNCTION
ObjFunction* compile(C_STR source) {
	Compiler compiler;

	scanner_init(source);
	initCompiler(&compiler, TYPE_SCRIPT);

	//init flags
	parser.hadError = false;
	parser.panicMode = false;

	advance();

	//keep compiling
	while (!match(TOKEN_EOF)) {
		declaration();
	}

	ObjFunction* function = endCompiler();
	freeLocals(&compiler);

	return parser.hadError ? NULL : function;
}

HOT_FUNCTION
void markCompilerRoots()
{
	Compiler* compiler = current;
	while (compiler != NULL) {
		markObject((Obj*)compiler->function);
		compiler = compiler->enclosing;
	}
}