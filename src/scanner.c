/*
 * MIT License
 * Copyright (c) 2025 IM&SD (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "scanner.h"

//shared scanner
Scanner scanner;

COLD_FUNCTION
void scanner_init(C_STR source)
{
	scanner.start = source;
	scanner.current = source;
	scanner.line = 1;
}

static bool isAlpha(char c) {
	return (c >= 'a' && c <= 'z') ||
		(c >= 'A' && c <= 'Z') ||
		c == '_';
}

static bool isDigit(char c) {
	return c >= '0' && c <= '9';
}

static bool isAtEnd()
{
	return *scanner.current == '\0';
}

static char advance() {
	scanner.current++;
	return scanner.current[-1];
}

static char peek() {
	return *scanner.current;
}

static char peekNext() {
	if (isAtEnd()) return '\0';
	return scanner.current[1];
}

static bool match(char expected) {
	if (isAtEnd()) return false;
	if (*scanner.current != expected) return false;
	scanner.current++;
	return true;
}

static Token makeToken(TokenType type) {
	return (Token) {
		.type = type,
			.start = scanner.start,
			.length = (uint32_t)(scanner.current - scanner.start),
			.line = scanner.line
	};
}

static Token errorToken(C_STR message) {
	return (Token) {
		.type = TOKEN_ERROR,
			.start = message,
			.length = (uint32_t)strlen(message),
			.line = scanner.line
	};
}

static TokenType skipWhitespace() {
	while (true) {
		char c = peek();
		switch (c) {
		case ' ':
		case '\r':
		case '\t':
			advance();
			break;
		case '\n':
			scanner.line++;
			advance();
			break;
		case '/': {
			switch (peekNext())
			{
			case '/': {
				// A comment goes until the end of the line.
				while (peek() != '\n' && !isAtEnd()) advance();
				break;
			}
			case '*': {
				// A comment goes until */.
				advance();
				while (true) {
					while (peek() != '*' && !isAtEnd()) {
						if (peek() == '\n') scanner.line++;
						advance();
					}
					if (isAtEnd()) {
						return TOKEN_UNTERMINATED_COMMENT;
					}
					else if (peek() == '*') {
						advance();
						if (isAtEnd()) return TOKEN_UNTERMINATED_COMMENT;

						if (peek() == '/') {
							advance();
							break;//jump to outer while
						}
					}
				}
				break;
			}
			default: return TOKEN_ERROR;
			}
			break;
		}
		default:
			return TOKEN_IGNORE;
		}
	}
}

static TokenType checkKeyword(uint32_t start, uint32_t length, C_STR rest, TokenType type) {
	if (scanner.current - scanner.start == start + length &&
		memcmp(scanner.start + start, rest, length) == 0) {
		return type;
	}

	return TOKEN_IDENTIFIER;
}

static TokenType identifierType() {
	switch (scanner.start[0]) {
	case 'a': return checkKeyword(1, 2, "nd", TOKEN_AND);
	case 'b': return checkKeyword(1, 4, "reak", TOKEN_BREAK);
	case 'c': if (scanner.current - scanner.start > 1) {
		switch (scanner.start[1]) {
		case 'l': return checkKeyword(2, 3, "ass", TOKEN_CLASS);
		case 'o': {
			if ((scanner.current - scanner.start > 3) && (scanner.start[2] == 'n')) {
				switch (scanner.start[3])
				{
				case 's': return checkKeyword(4, 1, "t", TOKEN_CONST);
				case 't': return checkKeyword(4, 4, "inue", TOKEN_CONTINUE);
				}
			}
		}
		}
		break;
	}
	case 'e': return checkKeyword(1, 3, "lse", TOKEN_ELSE);
	case 'f': {
		if (scanner.current - scanner.start > 1) {
			switch (scanner.start[1]) {
			case 'a': return checkKeyword(2, 3, "lse", TOKEN_FALSE);
			case 'o': return checkKeyword(2, 1, "r", TOKEN_FOR);
			case 'u': return checkKeyword(2, 1, "n", TOKEN_FUN);
			}
		}
		break;
	}
	case 'i': return checkKeyword(1, 1, "f", TOKEN_IF);
	case 'n': return checkKeyword(1, 2, "il", TOKEN_NIL);
	case 'o': return checkKeyword(1, 1, "r", TOKEN_OR);
	case 'p': return checkKeyword(1, 4, "rint", TOKEN_PRINT);
	case 'r': return checkKeyword(1, 5, "eturn", TOKEN_RETURN);
	case 's': return checkKeyword(1, 4, "uper", TOKEN_SUPER);
	case 't': {
		if (scanner.current - scanner.start > 1) {
			switch (scanner.start[1]) {
			case 'h': {
				if (scanner.current - scanner.start > 2) {
					switch (scanner.start[2]) {
					case 'i':return checkKeyword(3, 1, "s", TOKEN_THIS);
					case 'r':return checkKeyword(3, 2, "ow", TOKEN_THROW);
					}
				}
				break;
			}
			case 'r': return checkKeyword(2, 2, "ue", TOKEN_TRUE);
			}
		}
		break;
	}
	case 'v': return checkKeyword(1, 2, "ar", TOKEN_VAR);
	case 'w': return checkKeyword(1, 4, "hile", TOKEN_WHILE);
	case 'l': return checkKeyword(1, 5, "ambda", TOKEN_LAMBDA);
	}

	return TOKEN_IDENTIFIER;
}

static TokenType checkModule(uint32_t start, uint32_t length, C_STR rest, TokenType type) {
	if (scanner.current - scanner.start == start + length &&
		memcmp(scanner.start + start, rest, length) == 0) {
		return type;
	}

	return TOKEN_NIL;
}

//convert
static TokenType builtinType() {
	if ((scanner.start[0] == '@') && (scanner.current - scanner.start > 1)) {
		switch (scanner.start[1])
		{
		case 'm':return checkModule(2, 3, "ath", TOKEN_MODULE_MATH);
		case 'a':return checkModule(2, 4, "rray", TOKEN_MODULE_ARRAY);
		case 'o':return checkModule(2, 5, "bject", TOKEN_MODULE_OBJECT);
		case 's':
			if (scanner.current - scanner.start > 2) {
				switch (scanner.start[2])
				{
				case 't':return checkModule(3, 4, "ring", TOKEN_MODULE_STRING);
				case 'y':return checkModule(3, 4, "stem", TOKEN_MODULE_SYSTEM);
				}
			}
		case 't':return checkModule(2, 3, "ime", TOKEN_MODULE_TIME);
		case 'f':return checkModule(2, 3, "ile", TOKEN_MODULE_FILE);
		case 'g':return checkModule(2, 5, "lobal", TOKEN_MODULE_GLOBAL);
		}
	}

	return TOKEN_NIL;
}

static Token mention() {
	while (isAlpha(peek()) || isDigit(peek())) advance();

	TokenType type = builtinType();

	if (type == TOKEN_NIL) {
		return errorToken("Unexpected module (Available modules : @global, @math, @array, @object, @string, @time, @file, @system).");
	}

	return makeToken(type);
}

static Token identifier() {
	while (isAlpha(peek()) || isDigit(peek())) advance();
	return makeToken(identifierType());
}

static bool isBinDigit(char c) {
	return c == '0' || c == '1';
}

static bool isHexDigit(char c) {
	return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static Token number() {
	switch (peek())
	{
	case 'b':
	case 'B':
		advance(); // Skip '0'
		advance(); // Skip 'b' or 'B'

		if (!isBinDigit(peek())) {
			return errorToken("Invalid bin number format.");
		}

		do {
			advance();
		} while (isBinDigit(peek()));

		return makeToken(TOKEN_NUMBER_BIN);
	case 'x':
	case 'X':
		advance(); // Skip '0'
		advance(); // Skip 'x' or 'X'

		if (!isHexDigit(peek())) {
			return errorToken("Invalid hex number format.");
		}

		do {
			advance();
		} while (isHexDigit(peek()));

		return makeToken(TOKEN_NUMBER_HEX);
	default:
		break;
	}

	// Parse the number based on its base.
	// Decimal number.
	while (isDigit(peek())) {
		advance();
	}

	// Look for a fractional part.
	if (peek() == '.' && isDigit(peekNext())) {
		// Consume the ".".
		advance();

		while (isDigit(peek())) {
			advance();
		}
	}

	// Look for scientific notation (e or E).
	if (peek() == 'e' || peek() == 'E') {
		// Consume the 'e' or 'E'.
		advance();

		// Check for an optional sign (+ or -) in the exponent.
		if (peek() == '+' || peek() == '-') {
			advance(); // Consume the sign.
		}

		// Ensure there are digits in the exponent.
		if (!isDigit(peek())) {
			return errorToken("Expected digit after 'e' or 'E'.");
		}

		// Parse the exponent digits.
		while (isDigit(peek())) {
			advance();
		}
	}

	return makeToken(TOKEN_NUMBER);
}

static Token string() {
	bool isEscapeString = false;

	for (char c = '\0'; (c = peek()) != '"' && !isAtEnd(); advance()) {
		switch (c)
		{
		case '\n':
			scanner.line++;
			break;
		case '\\': {
			if (isAtEnd()) return errorToken("Unterminated string.");

			advance(); // skip "\\"

			if (!isEscapeString) {
				switch (peek()) {
				case '"':  // '\"'
				case '\\': // '\\'
					isEscapeString = true;
				default:
					break;
				}
			}
		}
		default:
			break;
		}
	}

	if (isAtEnd()) return errorToken("Unterminated string.");

	// The closing quote.
	advance();
	return makeToken(isEscapeString ? TOKEN_STRING_ESCAPE : TOKEN_STRING);
}

Token scanToken()
{
	//we don't need this
	switch (skipWhitespace())
	{
	case TOKEN_UNTERMINATED_COMMENT:
		return errorToken("Expect '*/' after comment.");
	default:
		break;
	}

	scanner.start = scanner.current;

	if (isAtEnd()) return makeToken(TOKEN_EOF);

	char c = advance();

	if (isAlpha(c)) return identifier();
	if (isDigit(c)) return number();

	switch (c) {
	case '(': return makeToken(TOKEN_LEFT_PAREN);
	case ')': return makeToken(TOKEN_RIGHT_PAREN);
	case '{': return makeToken(TOKEN_LEFT_BRACE);
	case '}': return makeToken(TOKEN_RIGHT_BRACE);
	case '[': return makeToken(TOKEN_RIGHT_SQUARE_BRACKET);
	case ']': return makeToken(TOKEN_RIGHT_SQUARE_BRACKET);
	case ';': return makeToken(TOKEN_SEMICOLON);
	case ':': return makeToken(TOKEN_COLON);
	case ',': return makeToken(TOKEN_COMMA);
	case '.': return makeToken(TOKEN_DOT);
	case '-': return makeToken(TOKEN_MINUS);
	case '+': return makeToken(TOKEN_PLUS);
	case '/': return makeToken(TOKEN_SLASH);
	case '*': return makeToken(TOKEN_STAR);
	case '%': return makeToken(TOKEN_PERCENT);
	case '@':
		return isAlpha(peek()) ? mention() : errorToken("Expected module name after @.");
	case '!':
		return makeToken(
			match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
	case '=':
		return makeToken(
			match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
	case '<':
		return makeToken(
			match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
	case '>':
		return makeToken(
			match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
	case '"': return string();
	}

	if (!isAtEnd()) {
		advance();//stop deadly looping
		return errorToken("Unexpected character. ");
	}
	else {
		return makeToken(TOKEN_EOF);
	}
}