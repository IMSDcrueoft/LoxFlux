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
static void variable(bool canAssign);
static Token syntheticToken(C_STR text);

#if COMPILATION_TIME_OPTIMIZATION
//do optimize here
static void instructionOptimize();
#endif

static Chunk* currentChunk() {
	return &current->function->chunk;
}

static OPStack* currentOpStack() {
	return &current->stack;
}

//for which complex logical/control flows, merge is not used
static void clearOpStack() {
	opStack_clear(currentOpStack());
}

//emit and check opStack
static void emitOpStack(uint8_t byte, bool doCheck) {
	opStack_push(currentOpStack(), byte);

#if COMPILATION_TIME_OPTIMIZATION
	if (!doCheck) return;

#if DEBUG_PRINT_CODE
	//print before optimize
	disassembleOpStack(currentOpStack());
#endif

	instructionOptimize();
#endif
}

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

static void error(C_STR message) {
	errorAt(&parser.previous, message);
}

static void errorAtCurrent(C_STR message) {
	errorAt(&parser.current, message);
}

static void advance() {
	parser.previous = parser.current;

	while (true) {
		parser.current = scanToken();
		if (parser.current.type != TOKEN_ERROR) break;

		errorAtCurrent(parser.current.start);
	}
}

static void consume(TokenType type, C_STR message) {
	if (parser.current.type == type) {
		advance();
		return;
	}

	errorAtCurrent(message);
}

static bool check(TokenType type) {
	return parser.current.type == type;
}

static bool match(TokenType type) {
	if (!check(type)) return false;
	advance();
	return true;
}

static void emitByte(uint8_t byte) {
	chunk_write(currentChunk(), byte, parser.previous.line);
}

//dynamic args
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

static void emitConstantCommond(OpCode target, uint32_t index) {
	if (index <= UINT24_MAX) {
		emitBytes(4, target, (uint8_t)index, (uint8_t)(index >> 8), (uint8_t)(index >> 16));
	}
	else {
		error("Too many constants in chunk.");
	}
}

static int32_t emitJump(uint8_t instruction) {
	emitBytes(3, instruction, 0xff, 0xff);
	clearOpStack();
	return currentChunk()->count - 2;
}

// generate loop
static void emitLoop(int32_t loopStart) {
	emitByte(OP_LOOP);

	int32_t offset = currentChunk()->count - loopStart + 2;
	if (offset > UINT16_MAX) error("Loop body too large.");

	emitBytes(2, offset & 0xff, (offset >> 8) & 0xff);
	clearOpStack();
}

static void emitReturn() {
	if (current->type == TYPE_INITIALIZER) {
		//16bits index get_local
		emitBytes(4, OP_GET_LOCAL, 0, 0, OP_RETURN);
	}
	else {
		emitBytes(2, OP_NIL, OP_RETURN);
	}
	clearOpStack();
}

static uint32_t makeConstant(Value value) {
	if (IS_NUMBER(value)) {
		//deduplicate in pool
		NumberEntry* entry = getNumberEntryInPool(&value);

		if (entry->index == UINT32_MAX) {
			return (entry->index = addConstant(value) & UINT24_MAX);//set value and return
		}
		else {
			return entry->index;
		}
	}
	else if (IS_STRING(value)) {
		//find if string is in constant,because it is in pool
		StringEntry* entry = getStringEntryInPool(AS_STRING(value));

		if (entry->index == UINT32_MAX) {
			return (entry->index = addConstant(value) & UINT24_MAX);//set value and return
		}
		else {
			return entry->index;
		}
	}
	else {
		return addConstant(value);
	}
}

static void emitConstant(Value value) {
	emitConstantCommond(OP_CONSTANT, makeConstant(value));
	emitOpStack(OP_CONSTANT, false);
}

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

	opStack_init(&compiler->stack);

	//it's a function
	switch (type) {
	case TYPE_FUNCTION:
	case TYPE_METHOD:
	case TYPE_INITIALIZER:
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

	// the module don't work in global scope
	if (compiler->enclosing == NULL) {
		compiler->nestingDepth = 0;
	}
	else {
		compiler->nestingDepth = compiler->enclosing->nestingDepth + 1;
		if (compiler->nestingDepth == FUNCTION_MAX_NESTING) {
			error("Too many nested functions.");
		}
	}

	// run in block
	if (type == TYPE_MODULE) {
		compiler->scopeDepth = 1;
	}
}

static void freeLocals(Compiler* compiler) {
	FREE_ARRAY_NO_GC(Local, compiler->locals, compiler->localCapacity);
	compiler->locals = NULL;
	compiler->localCapacity = 0;
}

static ObjFunction* endCompiler() {
	emitReturn();
	opStack_free(currentOpStack());

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

static void beginScope() {
	current->scopeDepth++;
}

static void emitPopCount(uint16_t popCount) {
	if (popCount == 0) return;
	if (popCount > 1) { // 16-bit index
		emitBytes(3, OP_POP_N, (uint8_t)popCount, (uint8_t)(popCount >> 8));
	}
	else {
		emitByte(OP_POP);
	}
	clearOpStack();
}

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
			clearOpStack();
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

static uint32_t identifierConstant(Token* name) {
	return makeConstant(OBJ_VAL(copyString(name->start, name->length, false)));
}

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

static bool identifiersEqual(Token* a, Token* b) {
	if (a->length != b->length) return false;
	return memcmp(a->start, b->start, a->length) == 0;
}

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

static uint32_t parseVariable(C_STR errorMessage) {
	consume(TOKEN_IDENTIFIER, errorMessage);

	declareVariable();
	//it is a local one
	if (current->scopeDepth > 0) return 0;

	return identifierConstant(&parser.previous);
}

static void markInitialized(bool isConst) {
	if (current->scopeDepth == 0) return;
	//only defined local vars could be used
	current->locals[current->localCount - 1].depth = current->scopeDepth;
	current->locals[current->localCount - 1].isConst = isConst;
}

static void defineVariable(uint32_t global) {
	//it is a local one
	if (current->scopeDepth > 0) {
		markInitialized(false);
		return;
	}

	emitConstantCommond(OP_DEFINE_GLOBAL, global);
	clearOpStack();
}

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

static void and_(bool canAssign) {
	int32_t endJump = emitJump(OP_JUMP_IF_FALSE);

	emitPopCount(1);
	parsePrecedence(PREC_AND);

	patchJump(endJump);
}

static void expression() {
	parsePrecedence(PREC_ASSIGNMENT);
}

static void block() {
	while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
		declaration();
	}

	consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void function(FunctionType type) {
	Compiler compiler;
	initCompiler(&compiler, type);
	beginScope();

	//check '('
	consume(TOKEN_LEFT_PAREN, "Expect '(' before function parameters.");

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

	consume(TOKEN_RIGHT_PAREN, "Expect ')' after function parameters.");

	if (!match(TOKEN_RIGHT_ARROW)) {
		consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");
		block();
	}
	else {
		if (type != TYPE_LAMBDA) {
			errorAtCurrent("'=>' can only be used after lambda parameters.");
			return;
		}

		expression();
		emitByte(OP_RETURN);
		clearOpStack();
	}

	ObjFunction* function = endCompiler();

	//create a closure
	emitConstantCommond(OP_CLOSURE, makeConstant(OBJ_VAL(function)));

	//insert upValue index
	for (uint32_t i = 0; i < function->upvalueCount; i++) {
		emitByte(compiler.upvalues[i].isLocal ? 1 : 0);
		emitBytes(2, (uint8_t)(compiler.upvalues[i].index), (uint8_t)(compiler.upvalues[i].index >> 8));
	}
	clearOpStack();

	freeLocals(&compiler);
}

static void lambda(bool canAssign) {
	FunctionType type = TYPE_LAMBDA;
	function(type);
}

static void method() {
	consume(TOKEN_IDENTIFIER, "Expect method name.");
	uint32_t constant = identifierConstant(&parser.previous);

	FunctionType type = TYPE_METHOD;
	if (parser.previous.length == 4 && memcmp(parser.previous.start, "init", 4) == 0) {
		type = TYPE_INITIALIZER;
	}
	function(type);
	emitConstantCommond(OP_METHOD, constant);
	clearOpStack();
}

static void classDeclaration() {
	consume(TOKEN_IDENTIFIER, "Expect class name.");
	Token className = parser.previous;

	uint32_t nameConstant = identifierConstant(&parser.previous);
	declareVariable();

	emitConstantCommond(OP_CLASS, nameConstant);
	clearOpStack();
	defineVariable(nameConstant);

	//link the chain
	ClassCompiler classCompiler = { .enclosing = currentClass,.hasSuperclass = false };
	currentClass = &classCompiler;

	//check class clsnname < super {}
	if (match(TOKEN_LESS)) {
		consume(TOKEN_IDENTIFIER, "Expect superclass name.");
		variable(false);

		if (identifiersEqual(&className, &parser.previous)) {
			error("A class can't inherit from itself.");
		}

		beginScope();
		addLocal(syntheticToken("super"));
		defineVariable(0);

		namedVariable(className, false);
		emitByte(OP_INHERIT);
		clearOpStack();

		//set flag
		classCompiler.hasSuperclass = true;
	}

	//put the class back to stack
	namedVariable(className, false);
	consume(TOKEN_LEFT_BRACE, "Expect '{' before class body.");
	while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
		method();
	}
	consume(TOKEN_RIGHT_BRACE, "Expect '}' after class body.");
	//pop out the class
	emitPopCount(1);

	//end the scope of super
	if (classCompiler.hasSuperclass) {
		endScope();
	}

	//resume
	currentClass = currentClass->enclosing;
}

static void funDeclaration() {
	uint32_t arg = parseVariable("Expect function name.");
	markInitialized(false);
	function(TYPE_FUNCTION);
	defineVariable(arg);
}

static void varDeclaration() {
	do {
		uint32_t arg = parseVariable("Expect variable name.");

		if (match(TOKEN_EQUAL)) {
			expression();
		}
		else {
			emitByte(OP_NIL);
			clearOpStack();
		}

		defineVariable(arg);

		if (parser.hadError) return;
	} while (match(TOKEN_COMMA));

	consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
}

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

static void expressionStatement() {
	expression();
	consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
	emitPopCount(1);
}

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
		emitPopCount(1);

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

static void branchStatement() {
	consume(TOKEN_LEFT_BRACE, "Expect '{' after 'branch'.");
	branchCaseStatement();
}

static void printStatement() {
	expression();
	consume(TOKEN_SEMICOLON, "Expect ';' after value.");
	emitByte(OP_PRINT);
	clearOpStack();
}

static void throwStatement() {
	expression();
	consume(TOKEN_SEMICOLON, "Expect ';' after value.");
	emitByte(OP_THROW);
	clearOpStack();
}

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
		clearOpStack();
	}
}

//exports in module script
static void exportsStatement() {
	if (current->type != TYPE_MODULE) {
		error("Only module can use 'export'.");
	}

	if (match(TOKEN_SEMICOLON)) {
		emitReturn();
	}
	else {
		expression();
		consume(TOKEN_SEMICOLON, "Expect ';' after export value.");
		emitByte(OP_RETURN);
		clearOpStack();
	}
}

static void import_(bool canAssign) {
	//parse the module name
	expression();

	//emit the import command
	emitByte(OP_IMPORT);
	clearOpStack();
}

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

static void doWhileStatement() {
	int32_t loopStart = currentChunk()->count;
	//record the loop
	LoopContext loop = (LoopContext){ .start = loopStart, .enclosing = current->currentLoop,.breakJumps = NULL,.breakJumpCount = 0 ,.enterParamCount = current->localCount };
	loop.breakJumpCapacity = 8;
	loop.breakJumps = ALLOCATE_NO_GC(int32_t, loop.breakJumpCapacity);
	current->currentLoop = &loop;

	statement();

	consume(TOKEN_WHILE, "Expect 'while' after 'do' to form a valid 'do-while'.");
	consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");
	consume(TOKEN_SEMICOLON, "Expect ';' after 'do-while' loop.");

	int32_t exitJump = emitJump(OP_JUMP_IF_FALSE_POP);
	emitLoop(loopStart);
	patchJump(exitJump);

	while (loop.breakJumpCount > 0) {
		patchJump(loop.breakJumps[--loop.breakJumpCount]);
	}
	FREE_ARRAY(int32_t, loop.breakJumps, loop.breakJumpCapacity);
	current->currentLoop = current->currentLoop->enclosing;
}

//only the loop itself can fix the jump position
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
		case TOKEN_DO:
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
	else if (match(TOKEN_DO)) {
		doWhileStatement();
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
	else if (match(TOKEN_EXPORT)) {
		exportsStatement();
	}
	else {
		expressionStatement();
	}
}

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

static void binary(bool canAssign) {
	TokenType operatorType = parser.previous.type;
	ParseRule* rule = getRule(operatorType);
	parsePrecedence((Precedence)(rule->precedence + 1));

	switch (operatorType) {
	case TOKEN_PLUS: {
		emitByte(OP_ADD);
		emitOpStack(OP_ADD, true);
		break;
	}
	case TOKEN_MINUS: {
		emitByte(OP_SUBTRACT);
		emitOpStack(OP_SUBTRACT, true);
		break;
	}
	case TOKEN_STAR: {
		emitByte(OP_MULTIPLY);
		emitOpStack(OP_MULTIPLY, true);
		break;
	}
	case TOKEN_SLASH: {
		emitByte(OP_DIVIDE);
		emitOpStack(OP_DIVIDE, true);
		break;
	}
	case TOKEN_PERCENT: {
		emitByte(OP_MODULUS);
		emitOpStack(OP_MODULUS, true);
		break;
	}
	case TOKEN_BANG_EQUAL: {
		emitByte(OP_NOT_EQUAL);
		emitOpStack(OP_NOT_EQUAL, true);
		break;
	}
	case TOKEN_EQUAL_EQUAL: {
		emitByte(OP_EQUAL);
		emitOpStack(OP_EQUAL, true);
		break;
	}
	case TOKEN_GREATER: {
		emitByte(OP_GREATER);
		emitOpStack(OP_GREATER, true);
		break;
	}
	case TOKEN_GREATER_EQUAL: {
		emitByte(OP_GREATER_EQUAL);
		emitOpStack(OP_GREATER_EQUAL, true);
		break;
	}
	case TOKEN_LESS: {
		emitByte(OP_LESS);
		emitOpStack(OP_LESS, true);
		break;
	}
	case TOKEN_LESS_EQUAL: {
		emitByte(OP_LESS_EQUAL);
		emitOpStack(OP_LESS_EQUAL, true);
		break;
	}
	case TOKEN_INSTANCE_OF: {
		emitByte(OP_INSTANCE_OF);
		clearOpStack();
		break;
	}
	case TOKEN_BIT_AND: {
		emitBytes(2, OP_BITWISE, BIT_OP_AND);
		clearOpStack();
		break;
	}
	case TOKEN_BIT_OR: {
		emitBytes(2, OP_BITWISE, BIT_OP_OR);
		clearOpStack();
		break;
	}
	case TOKEN_BIT_XOR: {
		emitBytes(2, OP_BITWISE, BIT_OP_XOR);
		clearOpStack();
		break;
	}
	case TOKEN_BIT_SHL: {
		emitBytes(2, OP_BITWISE, BIT_OP_SHL);
		clearOpStack();
		break;
	}
	case TOKEN_BIT_SHR: {
		emitBytes(2, OP_BITWISE, BIT_OP_SHR);
		clearOpStack();
		break;
	}
	case TOKEN_BIT_SAR: {
		emitBytes(2, OP_BITWISE, BIT_OP_SAR);
		clearOpStack();
		break;
	}
	default: return; // Unreachable.
	}
}

static void call(bool canAssign) {
	uint8_t argCount = argumentList();
	emitBytes(2, OP_CALL, argCount);
	clearOpStack();
}

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
	clearOpStack();
}

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
	clearOpStack();
}

static void objectLiteral(bool canAssign) {
	if (current->objectNestingDepth == OBJECT_MAX_NESTING) {
		error("Too many nested objects.");
		return;
	}

	++current->objectNestingDepth;
	emitByte(OP_NEW_OBJECT);
	clearOpStack();

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
			clearOpStack();

			if (parser.hadError) {
				--current->objectNestingDepth;
				return;
			}
		} while (match(TOKEN_COMMA));
	}

	consume(TOKEN_RIGHT_BRACE, "Expect '}' to close the object.");
	--current->objectNestingDepth;
}

//when it is constant index,use this
static void mergeSubscript(bool isAssignment) {
	Chunk* chunk = currentChunk();
#define READ_CONSTANT(index) (vm.constants.values[(index)])
#define READ_24BITS_INDEX()	\
	(((uint32_t)chunk->code[chunk->count - 1] << 16) +	\
	 ((uint32_t)chunk->code[chunk->count - 2] << 8) +	\
	 (uint32_t)(chunk->code[chunk->count - 3]))

	uint32_t index = READ_24BITS_INDEX();
	Value val = READ_CONSTANT(index);

	//must fallback
	chunk_fallback(chunk, 4);
	clearOpStack();

	if (isAssignment) expression();

	if (IS_NUMBER(val)) {
		emitConstantCommond(isAssignment ? OP_SET_INDEX : OP_GET_INDEX, index);
		clearOpStack();
	}
	else if (IS_STRING(val)) {
		emitConstantCommond(isAssignment ? OP_SET_PROPERTY : OP_GET_PROPERTY, index);
		clearOpStack();
	}
	else {
		error("Can only subscript with string or number.\n");
	}
#undef READ_CONSTANT
#undef READ_24BITS_INDEX
}

static void subscript(bool canAssign) {
	expression();
	consume(TOKEN_RIGHT_SQUARE_BRACKET, "Expect ']' after subscript.");

	// Because of the order of parsing, the index must be processed here
	uint8_t code = opStack_peek(currentOpStack(), 0);
	bool isAssignment = canAssign && match(TOKEN_EQUAL);

	if (code == OP_CONSTANT) {// constant index
		mergeSubscript(isAssignment);
	}
	else {
		if (isAssignment) {
			expression();
			emitByte(OP_SET_SUBSCRIPT);
			clearOpStack();
		}
		else {
			emitByte(OP_GET_SUBSCRIPT);
			clearOpStack();
		}
	}
}

//check builtin
static void builtinLiteral(bool canAssign) {
	switch (parser.previous.type)
	{
	case TOKEN_MODULE_MATH: {
		emitBytes(2, OP_MODULE_BUILTIN, MODULE_MATH);
		clearOpStack();
		break;
	}
	case TOKEN_MODULE_ARRAY: {
		emitBytes(2, OP_MODULE_BUILTIN, MODULE_ARRAY);
		clearOpStack();
		break;
	}
	case TOKEN_MODULE_OBJECT: {
		emitBytes(2, OP_MODULE_BUILTIN, MODULE_OBJECT);
		clearOpStack();
		break;
	}
	case TOKEN_MODULE_STRING: {
		emitBytes(2, OP_MODULE_BUILTIN, MODULE_STRING);
		clearOpStack();
		break;
	}
	case TOKEN_MODULE_TIME: {
		emitBytes(2, OP_MODULE_BUILTIN, MODULE_TIME);
		clearOpStack();
		break;
	}
	case TOKEN_MODULE_CTOR: {
		emitBytes(2, OP_MODULE_BUILTIN, MODULE_CTOR);
		clearOpStack();
		break;
	}
	case TOKEN_MODULE_SYSTEM: {
		emitBytes(2, OP_MODULE_BUILTIN, MODULE_SYSTEM);
		clearOpStack();
		break;
	}
	default: {
		emitByte(OP_NIL);
		clearOpStack();
		break;
	}
	}
}

static void literal(bool canAssign) {
	switch (parser.previous.type) {
	case TOKEN_FALSE: {
		emitByte(OP_FALSE);
		emitOpStack(OP_FALSE, false);
		break;
	}
	case TOKEN_NIL: {
		emitByte(OP_NIL);
		emitOpStack(OP_FALSE, false);
		break;
	}
	case TOKEN_TRUE: {
		emitByte(OP_TRUE);
		emitOpStack(OP_TRUE, false);
		break;
	}
	default: return; // Unreachable.
	}
}

static void grouping(bool canAssign) {
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void emitNumber(double value) {
	emitConstant(NUMBER_VAL(value));
}

static void number(bool canAssign) {
	emitNumber(strtod(parser.previous.start, NULL));
}

static void number_bin(bool canAssign) {
	emitNumber(strtol(parser.previous.start + 2, NULL, 2));
}

static void number_hex(bool canAssign) {
	emitNumber(strtol(parser.previous.start + 2, NULL, 16));
}

static void or_(bool canAssign) {
	//if true
	int32_t ifJump = emitJump(OP_JUMP_IF_TRUE);
	emitPopCount(1);
	parsePrecedence(PREC_OR);
	patchJump(ifJump);
}

static void string(bool canAssign) {
	emitConstant(OBJ_VAL(copyString(parser.previous.start + 1, parser.previous.length - 2, false)));
}

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
			emitOpStack(OP_SET_LOCAL, true);
		}
		else { // 16-bit index
			emitBytes(3, OP_GET_LOCAL, (uint8_t)arg, (uint8_t)(arg >> 8));
			emitOpStack(OP_GET_LOCAL, false);
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
			clearOpStack();
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
			clearOpStack();
		}
	}
}

static void variable(bool canAssign) {
	namedVariable(parser.previous, canAssign);
}

static Token syntheticToken(C_STR text) {
	return (Token) {
		.start = text, .length = strlen(text)
	};
}

static void this_(bool canAssign) {
	if (currentClass == NULL) {
		error("Can't use 'this' outside of a class.");
		return;
	}

	variable(false);
}

static void super_(bool canAssign) {
	if (currentClass == NULL) {
		error("Can't use 'super' outside of a class.");
	}
	else if (!currentClass->hasSuperclass) {
		error("Can't use 'super' in a class with no superclass.");
	}

	consume(TOKEN_DOT, "Expect '.' after 'super'.");
	consume(TOKEN_IDENTIFIER, "Expect superclass method name.");
	uint32_t name = identifierConstant(&parser.previous);

	namedVariable(syntheticToken("this"), false);//load instance

	if (match(TOKEN_LEFT_PAREN)) {
		uint8_t argCount = argumentList();
		namedVariable(syntheticToken("super"), false);
		emitConstantCommond(OP_SUPER_INVOKE, name);//super call
		emitByte(argCount);
	}
	else {
		namedVariable(syntheticToken("super"), false);//load class
		emitConstantCommond(OP_GET_SUPER, name);//get method
	}
	clearOpStack();
}

static void string_escape(bool canAssign) {
	emitConstant(OBJ_VAL(copyString(parser.previous.start + 1, parser.previous.length - 2, true)));
}

static void unary(bool canAssign) {
	TokenType operatorType = parser.previous.type;

	// Compile the operand.
	parsePrecedence(PREC_UNARY);

	// Emit the operator instruction.
	switch (operatorType) {
	case TOKEN_BANG: {
		emitByte(OP_NOT);
		emitOpStack(OP_NOT, true);
		break;
	}
	case TOKEN_MINUS: {
		emitByte(OP_NEGATE);
		emitOpStack(OP_NEGATE, true);
		break;
	}
	case TOKEN_BIT_NOT: {
		emitBytes(2, OP_BITWISE, BIT_OP_NOT);
		clearOpStack();
		break;
	}
	case TOKEN_TYPE_OF: {
		emitByte(OP_TYPE_OF);
		clearOpStack();
		break;
	}
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
	[TOKEN_MODULE_MATH] = {builtinLiteral,     NULL,   PREC_NONE},
	[TOKEN_MODULE_ARRAY] = {builtinLiteral,     NULL,   PREC_NONE},
	[TOKEN_MODULE_OBJECT] = {builtinLiteral,     NULL,   PREC_NONE},
	[TOKEN_MODULE_STRING] = {builtinLiteral,     NULL,   PREC_NONE},
	[TOKEN_MODULE_TIME] = {builtinLiteral,     NULL,   PREC_NONE},
	[TOKEN_MODULE_CTOR] = {builtinLiteral,     NULL,   PREC_NONE},
	[TOKEN_MODULE_SYSTEM] = {builtinLiteral,     NULL,   PREC_NONE},
	[TOKEN_AND] = {NULL,     and_,   PREC_AND},
	[TOKEN_CLASS] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_ELSE] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_FALSE] = {literal,     NULL,   PREC_NONE},
	[TOKEN_FOR] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_FUN] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_LAMBDA] = {lambda,	NULL,	PREC_NONE},
	[TOKEN_RIGHT_ARROW] = {NULL,	NULL,	PREC_NONE},
	[TOKEN_IF] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_BRANCH] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_NONE] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_NIL] = {literal,     NULL,   PREC_NONE},
	[TOKEN_OR] = {NULL,     or_,   PREC_OR},
	[TOKEN_PRINT] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_THROW] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_RETURN] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_SUPER] = {super_,     NULL,   PREC_NONE},
	[TOKEN_THIS] = {this_,     NULL,   PREC_NONE},
	[TOKEN_TRUE] = {literal,     NULL,   PREC_NONE},
	[TOKEN_VAR] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_CONST] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_DO] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_WHILE] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_ERROR] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_EOF] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_BREAK] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_CONTINUE] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_IMPORT] = {import_,     NULL,   PREC_NONE},
	[TOKEN_EXPORT] = {NULL,     NULL,   PREC_NONE},
};

static ParseRule* getRule(TokenType type) {
	return &rules[type];
}

ObjFunction* compile(C_STR source, FunctionType compileType) {
	Compiler compiler;

	scanner_init(source);
	initCompiler(&compiler, compileType);

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

void markCompilerRoots()
{
	Compiler* compiler = current;
	while (compiler != NULL) {
		markObject((Obj*)compiler->function);
		compiler = compiler->enclosing;
	}
}

/*
* do optimize here
* This part uses hard-coded instruction lengths, which needs to be noted
*/
#if COMPILATION_TIME_OPTIMIZATION
//COLD_FUNCTION
static void instructionOptimize() {
	//do optimize here
	Chunk* chunk = currentChunk();
	OPStack* opStack = currentOpStack();

	//opStack_peek will handle overflow
	uint8_t code = opStack_peek(opStack, 0);
	uint8_t prevRight = opStack_peek(opStack, 1);
	uint8_t prevLeft = opStack_peek(opStack, 2);

	//Constant folding
	bool isLeftConstant = (prevLeft == OP_CONSTANT);
	bool isRightConstant = (prevRight == OP_CONSTANT);
	bool isBothConstant = (isLeftConstant && isRightConstant);
	// local
	bool isLeftLocal = (prevLeft == OP_GET_LOCAL);
	bool isRightLocal = (prevRight == OP_GET_LOCAL);
	bool isBothLocal = (isLeftLocal && isRightLocal);

#define CHUNK_PEEK(offset) chunk->code[chunk->count - (offset) - 1]
#define READ_CONSTANT(index) (vm.constants.values[(index)])
#define READ_24BITS_INDEX(offset)	\
	(((uint32_t)chunk->code[chunk->count - (offset) - 1] << 16) +	\
	 ((uint32_t)chunk->code[chunk->count - (offset) - 2] << 8) +	\
	 (uint32_t)(chunk->code[chunk->count - (offset) - 3]))

#define BINARY_CALC(left,right,op)								\
    do {														\
		if (IS_NUMBER(left) && IS_NUMBER(right)) {				\
			double val = AS_NUMBER(left) op AS_NUMBER(right);	\
			chunk_fallback(chunk, 1 + 4 + 4);			\
			opStack_fallback(opStack,3);				\
			emitConstant(NUMBER_VAL(val));				\
		} else {												\
			error("Operands must be numbers.");			\
		}														\
	} while (false)

#define BINARY_CMP(left,right,op)								\
    do {														\
		if (IS_NUMBER(left) && IS_NUMBER(right)) {				\
			bool val = AS_NUMBER(left) op AS_NUMBER(right);		\
			chunk_fallback(chunk, 1 + 4 + 4);			\
			opStack_fallback(opStack,3);				\
			emitByte(val ? OP_TRUE : OP_FALSE);			\
			emitOpStack(val ? OP_TRUE : OP_FALSE, false);	\
		} else {												\
			error("Operands must be numbers.");			\
		}														\
	} while (false)

	switch (code) {
	case OP_ADD: {
		if (isBothConstant) {
			uint32_t idx_left = READ_24BITS_INDEX(1 + 4);//add + const
			uint32_t idx_right = READ_24BITS_INDEX(1); //add

			Value left = READ_CONSTANT(idx_left);
			Value right = READ_CONSTANT(idx_right);

			if (IS_NUMBER(left) && IS_NUMBER(right)) {
				double val = AS_NUMBER(left) + AS_NUMBER(right);
				chunk_fallback(chunk, 1 + 4 + 4);//add + const + const
				opStack_fallback(opStack, 3);
				emitConstant(NUMBER_VAL(val));
			}
			else if (IS_STRING(left) && IS_STRING(right)) {
				ObjString* val = connectString(AS_STRING(left), AS_STRING(right));
				chunk_fallback(chunk, 1 + 4 + 4);//add + const + const
				opStack_fallback(opStack, 3);
				emitConstant(OBJ_VAL(val));
			}
			else {
				error("Operands must be two numbers or two strings.");
			}
		}
		else if (isRightConstant) {
			chunk_fallback(chunk, 1);//op
			clearOpStack();
			CHUNK_PEEK(3) = OP_ADD_CONST; //convert command
		}
		else if (isRightLocal) {
			chunk_fallback(chunk, 1);//op
			clearOpStack();
			CHUNK_PEEK(2) = OP_ADD_LOCAL; //convert command
		}
		break;
	}
	case OP_SUBTRACT: {
		if (isBothConstant) {
			uint32_t idx_left = READ_24BITS_INDEX(1 + 4);//op + const
			uint32_t idx_right = READ_24BITS_INDEX(1); //op

			Value left = READ_CONSTANT(idx_left);
			Value right = READ_CONSTANT(idx_right);
			BINARY_CALC(left, right, -);
		}
		else if (isRightConstant) {
			chunk_fallback(chunk, 1);//op
			clearOpStack();
			CHUNK_PEEK(3) = OP_SUBTRACT_CONST; //convert command
		}
		else if (isRightLocal) {
			chunk_fallback(chunk, 1);//op
			clearOpStack();
			CHUNK_PEEK(2) = OP_SUBTRACT_LOCAL; //convert command
		}
		break;
	}
	case OP_MULTIPLY: {
		if (isBothConstant) {
			uint32_t idx_left = READ_24BITS_INDEX(1 + 4);//op + const
			uint32_t idx_right = READ_24BITS_INDEX(1); //op

			Value left = READ_CONSTANT(idx_left);
			Value right = READ_CONSTANT(idx_right);
			BINARY_CALC(left, right, *);
		}
		else if (isRightConstant) {
			chunk_fallback(chunk, 1);//op
			clearOpStack();
			CHUNK_PEEK(3) = OP_MULTIPLY_CONST; //convert command
		}
		else if (isRightLocal) {
			chunk_fallback(chunk, 1);//op
			clearOpStack();
			CHUNK_PEEK(2) = OP_MULTIPLY_LOCAL; //convert command
		}
		break;
	}
	case OP_DIVIDE: {
		if (isBothConstant) {
			uint32_t idx_left = READ_24BITS_INDEX(1 + 4);//op + const
			uint32_t idx_right = READ_24BITS_INDEX(1); //op

			Value left = READ_CONSTANT(idx_left);
			Value right = READ_CONSTANT(idx_right);
			BINARY_CALC(left, right, / );
		}
		else if (isRightConstant) {
			chunk_fallback(chunk, 1);//op
			clearOpStack();
			CHUNK_PEEK(3) = OP_DIVIDE_CONST; //convert command
		}
		else if (isRightLocal) {
			chunk_fallback(chunk, 1);//op
			clearOpStack();
			CHUNK_PEEK(2) = OP_DIVIDE_LOCAL; //convert command
		}
		break;
	}
	case OP_MODULUS: {
		if (isBothConstant) {
			uint32_t idx_left = READ_24BITS_INDEX(1 + 4);//op + const
			uint32_t idx_right = READ_24BITS_INDEX(1); //op

			Value left = READ_CONSTANT(idx_left);
			Value right = READ_CONSTANT(idx_right);

			if (IS_NUMBER(left) && IS_NUMBER(right)) {
				double val = fmod(AS_NUMBER(left), AS_NUMBER(right));
				chunk_fallback(chunk, 1 + 4 + 4);//op + const + const
				opStack_fallback(opStack, 3);
				emitConstant(NUMBER_VAL(val));
			}
			else {
				error("Operands must be numbers.");
			}
		}
		else if (isRightConstant) {
			chunk_fallback(chunk, 1);//op
			clearOpStack();
			CHUNK_PEEK(3) = OP_MODULUS_CONST; //convert command
		}
		else if (isRightLocal) {
			chunk_fallback(chunk, 1);//op
			clearOpStack();
			CHUNK_PEEK(2) = OP_MODULUS_LOCAL; //convert command
		}
		break;
	}
	case OP_NOT: {
		//all constants are true
		if (isRightConstant) {
			chunk_fallback(chunk, 1 + 4);//op + const
			opStack_fallback(opStack, 2);
			emitByte(OP_FALSE);
			emitOpStack(OP_FALSE, false);
		}
		else if (prevRight == OP_FALSE || prevRight == OP_NIL) {
			chunk_fallback(chunk, 1 + 1);//op + op
			opStack_fallback(opStack, 2);
			emitByte(OP_TRUE);
			emitOpStack(OP_TRUE, false);
		}
		else if (prevRight == OP_TRUE) {
			chunk_fallback(chunk, 1 + 1);//op + op
			opStack_fallback(opStack, 2);
			emitByte(OP_FALSE);
			emitOpStack(OP_FALSE, false);
		}
		else if (isRightLocal) {
			chunk_fallback(chunk, 1);//op
			clearOpStack();
			CHUNK_PEEK(2) = OP_NOT_LOCAL; //convert command
		}
		break;
	}
	case OP_NEGATE: {
		if (isRightConstant) {
			uint32_t idx_right = READ_24BITS_INDEX(1); //op
			Value right = READ_CONSTANT(idx_right);

			if (IS_NUMBER(right)) {
				double val = -AS_NUMBER(right);
				chunk_fallback(chunk, 1 + 4);//op + const
				opStack_fallback(opStack, 2);
				emitConstant(NUMBER_VAL(val));
			}
			else {
				error("Operand must be a number.");
			}
		}
		else if (prevRight == OP_FALSE || prevRight == OP_TRUE || prevRight == OP_NIL) {
			error("Operand must be a number.");
		}
		else if (isRightLocal) {
			chunk_fallback(chunk, 1);//op
			clearOpStack();
			CHUNK_PEEK(2) = OP_NEGATE_LOCAL; //convert command
		}
		break;
	}
	case OP_EQUAL: {
		if (isBothConstant) {
			uint32_t idx_left = READ_24BITS_INDEX(1 + 4);//op + const
			uint32_t idx_right = READ_24BITS_INDEX(1); //op

			Value left = READ_CONSTANT(idx_left);
			Value right = READ_CONSTANT(idx_right);

			bool val = valuesEqual(left, right);
			chunk_fallback(chunk, 1 + 4 + 4);//op + const + const
			opStack_fallback(opStack, 3);
			emitByte(val ? OP_TRUE : OP_FALSE);
			emitOpStack(val ? OP_TRUE : OP_FALSE, false);
		}
		else if (isRightConstant) {
			chunk_fallback(chunk, 1);//op
			clearOpStack();
			CHUNK_PEEK(3) = OP_EQUAL_CONST; //convert command
		}
		else if (isRightLocal) {
			chunk_fallback(chunk, 1);//op
			clearOpStack();
			CHUNK_PEEK(2) = OP_EQUAL_LOCAL; //convert command
		}
		break;
	}
	case OP_NOT_EQUAL: {
		if (isBothConstant) {
			uint32_t idx_left = READ_24BITS_INDEX(1 + 4);//op + const
			uint32_t idx_right = READ_24BITS_INDEX(1); //op

			Value left = READ_CONSTANT(idx_left);
			Value right = READ_CONSTANT(idx_right);

			bool val = !valuesEqual(left, right);
			chunk_fallback(chunk, 1 + 4 + 4);//op + const + const
			opStack_fallback(opStack, 3);
			emitByte(val ? OP_TRUE : OP_FALSE);
			emitOpStack(val ? OP_TRUE : OP_FALSE, false);
		}
		else if (isRightConstant) {
			chunk_fallback(chunk, 1);//op
			clearOpStack();
			CHUNK_PEEK(3) = OP_NOT_EQUAL_CONST; //convert command
		}
		else if (isRightLocal) {
			chunk_fallback(chunk, 1);//op
			clearOpStack();
			CHUNK_PEEK(2) = OP_NOT_EQUAL_LOCAL; //convert command
		}
		break;
	}
	case OP_GREATER: {
		if (isBothConstant) {
			uint32_t idx_left = READ_24BITS_INDEX(1 + 4);//op + const
			uint32_t idx_right = READ_24BITS_INDEX(1); //op

			Value left = READ_CONSTANT(idx_left);
			Value right = READ_CONSTANT(idx_right);
			BINARY_CMP(left, right, > );
		}
		else if (isRightConstant) {
			chunk_fallback(chunk, 1);//op
			clearOpStack();
			CHUNK_PEEK(3) = OP_GREATER_CONST; //convert command
		}
		else if (isRightLocal) {
			chunk_fallback(chunk, 1);//op
			clearOpStack();
			CHUNK_PEEK(2) = OP_GREATER_LOCAL; //convert command
		}
		break;
	}
	case OP_LESS: {
		if (isBothConstant) {
			uint32_t idx_left = READ_24BITS_INDEX(1 + 4);//op + const
			uint32_t idx_right = READ_24BITS_INDEX(1); //op

			Value left = READ_CONSTANT(idx_left);
			Value right = READ_CONSTANT(idx_right);
			BINARY_CMP(left, right, < );
		}
		else if (isRightConstant) {
			chunk_fallback(chunk, 1);//op
			clearOpStack();
			CHUNK_PEEK(3) = OP_LESS_CONST; //convert command
		}
		else if (isRightLocal) {
			chunk_fallback(chunk, 1);//op
			clearOpStack();
			CHUNK_PEEK(2) = OP_LESS_LOCAL; //convert command
		}
		break;
	}
	case OP_LESS_EQUAL: {
		if (isBothConstant) {
			uint32_t idx_left = READ_24BITS_INDEX(1 + 4);//op + const
			uint32_t idx_right = READ_24BITS_INDEX(1); //op

			Value left = READ_CONSTANT(idx_left);
			Value right = READ_CONSTANT(idx_right);
			BINARY_CMP(left, right, <= );
		}
		else if (isRightConstant) {
			chunk_fallback(chunk, 1);//op
			clearOpStack();
			CHUNK_PEEK(3) = OP_LESS_EQUAL_CONST; //convert command
		}
		else if (isRightLocal) {
			chunk_fallback(chunk, 1);//op
			clearOpStack();
			CHUNK_PEEK(2) = OP_LESS_EQUAL_LOCAL; //convert command
		}
		break;
	}
	case OP_GREATER_EQUAL: {
		if (isBothConstant) {
			uint32_t idx_left = READ_24BITS_INDEX(1 + 4);//op + const
			uint32_t idx_right = READ_24BITS_INDEX(1); //op

			Value left = READ_CONSTANT(idx_left);
			Value right = READ_CONSTANT(idx_right);
			BINARY_CMP(left, right, >= );
		}
		else if (isRightConstant) {
			chunk_fallback(chunk, 1);//op
			clearOpStack();
			CHUNK_PEEK(3) = OP_GREATER_EQUAL_CONST; //convert command
		}
		else if (isRightLocal) {
			chunk_fallback(chunk, 1);//op
			clearOpStack();
			CHUNK_PEEK(2) = OP_GREATER_EQUAL_LOCAL; //convert command
		}
		break;
	}
	case OP_SET_LOCAL: {
		break;
	}
	default: {
		fprintf(stderr, "Unexpected(%u)\n", code);
		break;
	}
	}

#undef CHUNK_PEEK
#undef READ_CONSTANT
#undef READ_24BITS_INDEX
#undef BINARY_CALC
#undef BINARY_CMP
}
#endif