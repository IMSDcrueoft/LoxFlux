#include "compiler.h"
#include "builtinModule.h"

#if DEBUG_PRINT_CODE
#include "debug.h"
#endif
#if LOG_COMPILE_TIMING
#include "timer.h"
#endif

//shared parser
Parser parser;
Compiler* current = NULL;
Chunk* compilingChunk = NULL;
LoopContext* currentLoop = NULL;

static Chunk* currentChunk() {
	return compilingChunk;
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

static int32_t emitJump(uint8_t instruction) {
	emitByte(instruction);
	emitBytes(2, 0xff, 0xff);
	return currentChunk()->count - 2;
}


// generate loop
static void emitLoop(int32_t loopStart) {
	emitByte(OP_LOOP);

	int32_t offset = currentChunk()->count - loopStart + 2;
	if (offset > UINT16_MAX) error("Loop body too large.");

	emitBytes(2, offset & 0xff, (offset >> 8) & 0xff);
}

static void emitReturn() {
	emitByte(OP_RETURN);
}

static uint32_t makeConstant(Value value) {
	switch (value.type) {
	case VAL_NUMBER: {
		//deduplicate in pool
		NumberEntry* entry = getNumberEntryInPool(&value);

		if (entry->index == UINT32_MAX) {
			return (entry->index = addConstant(currentChunk(), value) & UINT24_MAX);//set value and return
		}
		else {
			return entry->index;
		}
	}
	case VAL_OBJ: {
		switch (AS_OBJ(value)->type) {
		case OBJ_STRING: {
			//find if string is in constant,because it is in pool
			Entry* entry = getStringEntryInPool(AS_STRING(value));

			if (IS_BOOL(entry->value)) {
				uint32_t index = addConstant(currentChunk(), value) & UINT24_MAX;
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
			return addConstant(currentChunk(), value);
		}
		break;
	}
	default://won't be here, there is only num,bool,nil,obj
		return addConstant(currentChunk(), value);
	}
}

//we got very big range
static void emitConstantCommond(uint32_t index) {
	if (index <= UINT10_MAX) {  // 10-bit index (8 + 2 bits)
		emitBytes(2, OP_CONSTANT | (uint8_t)((index >> 8) << 6), (uint8_t)index);
	}
	else if (index <= UINT18_MAX) { // 18-bit index (16 + 2 bits)
		emitBytes(3, OP_CONSTANT_SHORT | (uint8_t)((index >> 16) << 6), (uint8_t)index, (uint8_t)(index >> 8));
	}
	else if (index <= UINT24_MAX) { // 24-bit index
		emitBytes(4, OP_CONSTANT_LONG, (uint8_t)index, (uint8_t)(index >> 8), (uint8_t)(index >> 16));
	}
	else {
		error("Too many constants in chunk.");
	}
}

static void emitConstant(Value value) {
	emitConstantCommond(makeConstant(value));
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

static void compiler_init(Compiler* compiler) {
	compiler->localCount = 0;
	compiler->scopeDepth = 0;
	//init with 256 slots
	compiler->locals = ALLOCATE(Local, UINT10_COUNT);
	compiler->capacity = UINT10_COUNT;

	current = compiler;
}

static void compiler_free(Compiler* compiler) {
	compiler->localCount = 0;
	compiler->scopeDepth = 0;
	FREE_ARRAY(Local, compiler->locals, compiler->capacity);
	compiler->locals = NULL;
	compiler->capacity = 0;

	current = compiler;
}


static void endCompiler() {
	emitReturn();

#if DEBUG_PRINT_CODE
	if (!parser.hadError) {
		disassembleChunk(currentChunk(), "code");
	}
#endif
}

static void beginScope() {
	current->scopeDepth++;
}

static void endScope() {
	current->scopeDepth--;

	uint32_t popCount = 0;

	while ((current->localCount > 0) && (current->locals[current->localCount - 1].depth > current->scopeDepth)) {
		++popCount;
		current->localCount--;
	}

	if (popCount == 0) return;

	if (popCount > 1) { // 10-bit index (8 + 2 bits)
		--popCount;     // the limit is 1024, but 10 bit can only expressed [0 to 1023] ,so i expand it to [1,1024]
		emitBytes(2, OP_POP_N | (uint8_t)((popCount >> 8) << 6), (uint8_t)popCount);
	}
	else {
		emitByte(OP_POP);
	}
}

//need to define first
static ParseRule* getRule(TokenType type);

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
	if (current->localCount == UINT10_COUNT) {
		error("Too many nested local variables in function.");
		return;
	}

	Local* local = &current->locals[current->localCount++];
	local->name = name;
	local->depth = -1;// var a = a;??? avoid this
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

	//it is a global defined var
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
	//only defined local vars could be used
	current->locals[current->localCount - 1].depth = current->scopeDepth;
	current->locals[current->localCount - 1].isConst = isConst;
}

static void emitGlobalCommond(OpCode commond, uint32_t index);
static void defineVariable(uint32_t global) {
	//it is a local one
	if (current->scopeDepth > 0) {
		markInitialized(false);
		return;
	}

	emitGlobalCommond(OP_DEFINE_GLOBAL, global);
}

static void defineConst(uint32_t global) {
	//it is a local one
	if (current->scopeDepth > 0) {
		markInitialized(true);
		return;
	}
	else {
		errorAtCurrent("Constant can only be defined in the global scope.");
	}
}

static void and_(bool canAssign) {
	int32_t endJump = emitJump(OP_JUMP_IF_FALSE);

	emitByte(OP_POP);
	parsePrecedence(PREC_AND);

	patchJump(endJump);
}

static void expression() {
	parsePrecedence(PREC_ASSIGNMENT);
}

static void declaration();
static void block() {
	while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
		declaration();
	}

	consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void varDeclaration() {
	uint32_t global = parseVariable("Expect variable name.");

	if (match(TOKEN_EQUAL)) {
		expression();
	}
	else {
		emitByte(OP_NIL);
	}
	consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

	defineVariable(global);
}

static void constDeclaration() {
	uint32_t global = parseVariable("Expect constant name.");

	if (!match(TOKEN_EQUAL)) {
		errorAtCurrent("Constant must be initialized.");
	}

	expression();
	consume(TOKEN_SEMICOLON, "Expect ';' after constant declaration.");

	defineConst(global);
}

static void expressionStatement() {
	expression();
	consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
	emitByte(OP_POP);
}

static void statement();
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
	LoopContext loop = (LoopContext){ .start = loopStart, .upper = currentLoop,.breakJumps = NULL,.breakJumpCount = 0 };
	loop.breakJumpCapacity = 8;
	loop.breakJumps = ALLOCATE(int32_t, loop.breakJumpCapacity);
	currentLoop = &loop;

	statement();
	emitLoop(loopStart);

	//if there is no exitJump,this is an infinite loop
	if (exitJump != -1) {
		patchJump(exitJump);
	}

	while (loop.breakJumpCount > 0) {
		patchJump(loop.breakJumps[--loop.breakJumpCount]);
	}

	FREE_ARRAY(int32_t, loop.breakJumps, loop.breakJumpCapacity);
	currentLoop = currentLoop->upper;
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

static void printStatement() {
	expression();
	consume(TOKEN_SEMICOLON, "Expect ';' after value.");
	emitByte(OP_PRINT);
}

static void whileStatement() {
	int32_t loopStart = currentChunk()->count;

	consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

	int32_t exitJump = emitJump(OP_JUMP_IF_FALSE_POP);

	//record the loop
	LoopContext loop = (LoopContext){ .start = loopStart, .upper = currentLoop,.breakJumps = NULL,.breakJumpCount = 0 };
	loop.breakJumpCapacity = 8;
	loop.breakJumps = ALLOCATE(int32_t, loop.breakJumpCapacity);
	currentLoop = &loop;

	statement();

	emitLoop(loopStart);

	patchJump(exitJump);

	while (loop.breakJumpCount > 0) {
		patchJump(loop.breakJumps[--loop.breakJumpCount]);
	}

	FREE_ARRAY(int32_t, loop.breakJumps, loop.breakJumpCapacity);
	currentLoop = currentLoop->upper;
}

//only the loop itself can fix the jump position
static void breakStatement() {
	if (currentLoop == NULL) {
		error("Cannot use 'break' outside of a loop.");
		return;
	}

	int32_t jump = emitJump(OP_JUMP);

	if (currentLoop->breakJumpCount == currentLoop->breakJumpCapacity) {
		if (currentLoop->breakJumpCapacity == UINT16_MAX) {
			error("Too many break statements in one loop.");
			return;
		}

		uint32_t oldCapacity = currentLoop->breakJumpCapacity;
		currentLoop->breakJumpCapacity = GROW_CAPACITY(oldCapacity);

		uint16_t newCapacity = GROW_CAPACITY(currentLoop->breakJumpCapacity);
		currentLoop->breakJumps = GROW_ARRAY(int32_t, currentLoop->breakJumps, oldCapacity, currentLoop->breakJumpCapacity);
	}

	currentLoop->breakJumps[currentLoop->breakJumpCount++] = jump;

	consume(TOKEN_SEMICOLON, "Expect ';' after 'break'.");
}

static void continueStatement() {
	if (currentLoop == NULL) {
		error("Cannot use 'continue' outside of a loop.");
		return;
	}

	emitLoop(currentLoop->start);
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
		case TOKEN_WHILE:
		case TOKEN_PRINT:
		case TOKEN_RETURN:
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
	else {
		expressionStatement();
	}
}

static void declaration() {
	//might went panicMode
	if (parser.panicMode) synchronize();

	if (match(TOKEN_VAR)) {
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
	default: return; // Unreachable.
	}
}

//check builtin
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

static void literal(bool canAssign) {
	switch (parser.previous.type) {
	case TOKEN_FALSE: emitByte(OP_FALSE); break;
	case TOKEN_NIL: emitByte(OP_NIL); break;
	case TOKEN_TRUE: emitByte(OP_TRUE); break;
	default: return; // Unreachable.
	}
}

static void grouping(bool canAssign) {
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

static void emitNumber(double value) {
	if (value == 0.0) {
		emitByte(OP_IMM | 0b00000000);
	}
	else if (value == 1.0) {
		emitByte(OP_IMM | 0b01000000);
	}
	else if (value == 2.0) {
		emitByte(OP_IMM | 0b10000000);
	}
	else if (value == 10.0) {
		emitByte(OP_IMM | 0b11000000);
	}
	else {
		emitConstant(NUMBER_VAL(value));
	}
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

static void string(bool canAssign) {
	emitConstant(OBJ_VAL(copyString(parser.previous.start + 1, parser.previous.length - 2, false)));
}

static void emitGlobalCommond(OpCode commond, uint32_t index) {
	if (index <= UINT8_MAX) {
		emitBytes(2, commond | 0b00000000, (uint8_t)index);
	}
	else if (index <= UINT16_MAX) {
		emitBytes(3, commond | 0b01000000, (uint8_t)index, (uint8_t)(index >> 8));
	}
	else if (index <= UINT24_MAX) {
		emitBytes(4, commond | 0b10000000, (uint8_t)index, (uint8_t)(index >> 8), (uint8_t)(index >> 16));
	}
	else {
		error("Too many constants in chunk.");
	}
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
			// 10-bit index (8 + 2 bits)
			emitBytes(2, OP_SET_LOCAL | (uint8_t)((arg >> 8) << 6), (uint8_t)arg);
		}
		else { // 10-bit index (8 + 2 bits)
			emitBytes(2, OP_GET_LOCAL | (uint8_t)((arg >> 8) << 6), (uint8_t)arg);
		}
	}
	else {//its global var
		arg = identifierConstant(&name);

		if (canAssign && match(TOKEN_EQUAL)) {
			expression();

			emitGlobalCommond(OP_SET_GLOBAL,arg);
		}
		else {
			emitGlobalCommond(OP_GET_GLOBAL,arg);
		}
	}
}

static void variable(bool canAssign) {
	namedVariable(parser.previous, canAssign);
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
	case TOKEN_BANG: emitByte(OP_NOT); break;
	case TOKEN_MINUS: emitByte(OP_NEGATE); break;
	default: return; // Unreachable.
	}
}

ParseRule rules[] = {
	[TOKEN_LEFT_PAREN] = {grouping, NULL,   PREC_NONE    },
	[TOKEN_RIGHT_PAREN] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_LEFT_BRACE] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_RIGHT_BRACE] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_COMMA] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_DOT] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_MINUS] = {unary   ,    binary, PREC_TERM     },
	[TOKEN_PLUS] = {NULL,     binary, PREC_TERM    },
	[TOKEN_SEMICOLON] = {NULL,     NULL,   PREC_NONE},
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
	[TOKEN_IF] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_NIL] = {literal,     NULL,   PREC_NONE},
	[TOKEN_OR] = {NULL,     or_,   PREC_OR},
	[TOKEN_PRINT] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_RETURN] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_SUPER] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_THIS] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_TRUE] = {literal,     NULL,   PREC_NONE},
	[TOKEN_VAR] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_CONST] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_WHILE] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_ERROR] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_EOF] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_BREAK] = {NULL,     NULL,   PREC_NONE},
	[TOKEN_CONTINUE] = {NULL,     NULL,   PREC_NONE},
};

static ParseRule* getRule(TokenType type) {
	return &rules[type];
}

bool compile(C_STR source, Chunk* chunk) {
#if LOG_COMPILE_TIMING
	uint64_t time_compile = get_nanoseconds();
#endif
	Compiler compiler;

	scanner_init(source);
	compiler_init(&compiler);

	//set pointer
	compilingChunk = chunk;

	//init flags
	parser.hadError = false;
	parser.panicMode = false;

	advance();

	//keep compiling
	while (!match(TOKEN_EOF)) {
		declaration();
	}

	//end
	endCompiler();
	compiler_free(&compiler);

#if LOG_COMPILE_TIMING
	double time_ms = (get_nanoseconds() - time_compile) * 1e-6;
	printf("Log: Finished compiling in %g ms.\n", time_ms);
#endif
	return !parser.hadError;
}