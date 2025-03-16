/*
 * MIT License
 * Copyright (c) 2025 IM&SD (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#pragma once
#include "common.h"

typedef enum {
	// Single-character tokens. 
	TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN,
	TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE,
	TOKEN_LEFT_SQUARE_BRACKET, TOKEN_RIGHT_SQUARE_BRACKET,
	TOKEN_COMMA, TOKEN_DOT, TOKEN_MINUS, TOKEN_PLUS,
	TOKEN_SEMICOLON, TOKEN_SLASH, TOKEN_STAR, TOKEN_PERCENT,
	// One or two character tokens. 
	TOKEN_BANG, TOKEN_BANG_EQUAL,
	TOKEN_EQUAL, TOKEN_EQUAL_EQUAL,
	TOKEN_GREATER, TOKEN_GREATER_EQUAL,
	TOKEN_LESS, TOKEN_LESS_EQUAL,
	// Literals. 
	TOKEN_IDENTIFIER, TOKEN_STRING, TOKEN_STRING_ESCAPE, TOKEN_NUMBER, TOKEN_NUMBER_BIN, TOKEN_NUMBER_HEX,
	// Builtin Literals. 
	TOKEN_MODULE_GLOBAL,
	TOKEN_MODULE_MATH,
	TOKEN_MODULE_ARRAY,
	TOKEN_MODULE_OBJECT,
	TOKEN_MODULE_STRING,
	TOKEN_MODULE_TIME,
	TOKEN_MODULE_FILE,
	TOKEN_MODULE_SYSTEM,
	// Keywords. 
	TOKEN_AND, TOKEN_CLASS, TOKEN_ELSE, TOKEN_FALSE,
	TOKEN_FOR, TOKEN_FUN, TOKEN_IF, TOKEN_NIL, TOKEN_OR,
	TOKEN_PRINT, TOKEN_RETURN, TOKEN_SUPER, TOKEN_THIS,
	TOKEN_TRUE, TOKEN_VAR, TOKEN_WHILE, TOKEN_CONST,
	TOKEN_BREAK, TOKEN_CONTINUE, TOKEN_COLON, TOKEN_THROW,

	TOKEN_ERROR, TOKEN_EOF, TOKEN_IGNORE, TOKEN_UNTERMINATED_COMMENT,
} TokenType;

typedef struct {
	uint32_t line;
	C_STR start;
	C_STR current;
} Scanner;

typedef struct {
	TokenType type;
	uint32_t length;
	uint32_t line;
	C_STR start;
} Token;

void scanner_init(C_STR source);

Token scanToken();