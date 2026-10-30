/*
 * MIT License
 * Copyright (c) 2025-2026 IMSDcrueoft (https://github.com/IMSDcrueoft)
 * See LICENSE file in the root directory for full license text.
*/
#include "scanner.h"

void scanner_init(Scanner* scanner, C_STR source)
{
	scanner->start = source;
	scanner->current = source;
	scanner->line = 1;
}

static bool isAlpha(char c) {
	return (c >= 'a' && c <= 'z') ||
		(c >= 'A' && c <= 'Z') ||
		c == '_';
}

static bool isDigit(char c) {
	return c >= '0' && c <= '9';
}

static bool isAtEnd(Scanner* scanner)
{
	return *scanner->current == '\0';
}

static char advance(Scanner* scanner) {
	scanner->current++;
	return scanner->current[-1];
}

static char peek(Scanner* scanner) {
	return *scanner->current;
}

static char peekNext(Scanner* scanner) {
	if (isAtEnd(scanner)) return '\0';
	return scanner->current[1];
}

static bool match(Scanner* scanner, char expected) {
	if (isAtEnd(scanner)) return false;
	if (*scanner->current != expected) return false;
	scanner->current++;
	return true;
}

static Token makeToken(Scanner* scanner, TokenType type) {
	return (Token) {
		.type = type,
			.start = scanner->start,
			.length = (uint32_t)(scanner->current - scanner->start),
			.line = scanner->line
	};
}

static Token errorToken(Scanner* scanner, C_STR message) {
	return (Token) {
		.type = TOKEN_ERROR,
			.start = message,
			.length = (uint32_t)strlen(message),
			.line = scanner->line
	};
}

static TokenType skipWhitespace(Scanner* scanner) {
	while (true) {
		char c = peek(scanner);
		switch (c) {
		case ' ':
		case '\r':
		case '\t':
			advance(scanner);
			break;
		case '\n':
			scanner->line++;
			advance(scanner);
			break;
		case '/': {
			switch (peekNext(scanner))
			{
			case '/': {
				// A comment goes until the end of the line.
				while (peek(scanner) != '\n' && !isAtEnd(scanner)) advance(scanner);
				break;
			}
			case '*': {
				// A comment goes until */.
				advance(scanner);
				while (true) {
					while (peek(scanner) != '*' && !isAtEnd(scanner)) {
						if (peek(scanner) == '\n') scanner->line++;
						advance(scanner);
					}
					if (isAtEnd(scanner)) {
						return TOKEN_UNTERMINATED_COMMENT;
					}
					else if (peek(scanner) == '*') {
						advance(scanner);
						if (isAtEnd(scanner)) return TOKEN_UNTERMINATED_COMMENT;

						if (peek(scanner) == '/') {
							advance(scanner);
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

static TokenType checkKeyword(Scanner* scanner, uint32_t start, uint32_t length, C_STR rest, TokenType type) {
	if (scanner->current - scanner->start == start + length &&
		memcmp(scanner->start + start, rest, length) == 0) {
		return type;
	}

	return TOKEN_IDENTIFIER;
}

static TokenType identifierType(Scanner* scanner) {
	switch (scanner->start[0]) {
	case 'a': return checkKeyword(scanner, 1, 2, "nd", TOKEN_AND);
	case 'b': {
		if ((scanner->current - scanner->start > 2) && (scanner->start[1] == 'r')) {
			switch (scanner->start[2]) {
			case 'e':return checkKeyword(scanner, 3, 2, "ak", TOKEN_BREAK);
			case 'a':return checkKeyword(scanner, 3, 3, "nch", TOKEN_BRANCH);
			}
		}
		break;
	}
	case 'c': {
		if (scanner->current - scanner->start > 1) {
			switch (scanner->start[1]) {
			case 'e': return checkKeyword(scanner, 2, 2, "de", TOKEN_CEDE);
			case 'l': return checkKeyword(scanner, 2, 3, "ass", TOKEN_CLASS);
			case 'o': {
				if ((scanner->current - scanner->start > 3) && (scanner->start[2] == 'n')) {
					switch (scanner->start[3])
					{
					case 's': return checkKeyword(scanner, 4, 1, "t", TOKEN_CONST);
					case 't': return checkKeyword(scanner, 4, 4, "inue", TOKEN_CONTINUE);
					}
				}
				break;
			}
			}
		}
		break;
	}
	case 'd': return checkKeyword(scanner, 1, 1, "o", TOKEN_DO);
	case 'e': {
		if (scanner->current - scanner->start > 1) {
			switch (scanner->start[1])
			{
			case 'l':return checkKeyword(scanner, 2, 2, "se", TOKEN_ELSE);
			case 'x':return checkKeyword(scanner, 2, 4, "port", TOKEN_EXPORT);
			}
		}
		break;
	}
	case 'f': {
		if (scanner->current - scanner->start > 1) {
			switch (scanner->start[1]) {
			case 'a': return checkKeyword(scanner, 2, 3, "lse", TOKEN_FALSE);
			case 'o': {
				if (scanner->current - scanner->start == 3) {
					return checkKeyword(scanner, 2, 1, "r", TOKEN_FOR);
				}
				else {
					return checkKeyword(scanner, 2, 5, "reach", TOKEN_FOREACH);
				}
			}
			case 'u': return checkKeyword(scanner, 2, 1, "n", TOKEN_FUN);
			}
		}
		break;
	}
	case 'i': {
		if (scanner->current - scanner->start > 1) {
			switch (scanner->start[1]) {
			case 'f': return checkKeyword(scanner, 2, 0, "", TOKEN_IF);
			case 'm': return checkKeyword(scanner, 2, 4, "port", TOKEN_IMPORT);
			case 'n': {
				if (scanner->current - scanner->start == 2) {
					return TOKEN_IN;
				}
				else {
					return checkKeyword(scanner, 2, 8, "stanceof", TOKEN_INSTANCE_OF);
				}
			}
			}
		}
		break;
	}
	case 'l': return checkKeyword(scanner, 1, 5, "ambda", TOKEN_LAMBDA);
	case 'n': {
		if (scanner->current - scanner->start > 1) {
			switch (scanner->start[1]) {
			case 'i': return checkKeyword(scanner, 2, 1, "l", TOKEN_NIL);
			case 'o': return checkKeyword(scanner, 2, 2, "ne", TOKEN_NONE);
			}
		}
		break;
	}
	case 'o': return checkKeyword(scanner, 1, 1, "r", TOKEN_OR);
	case 'p': return checkKeyword(scanner, 1, 4, "rint", TOKEN_PRINT);
	case 'r': return checkKeyword(scanner, 1, 5, "eturn", TOKEN_RETURN);
	case 's': return checkKeyword(scanner, 1, 4, "uper", TOKEN_SUPER);
	case 't': {
		if (scanner->current - scanner->start > 1) {
			switch (scanner->start[1]) {
			case 'h': {
				if (scanner->current - scanner->start > 2) {
					switch (scanner->start[2]) {
					case 'i':return checkKeyword(scanner, 3, 1, "s", TOKEN_THIS);
					case 'r':return checkKeyword(scanner, 3, 2, "ow", TOKEN_THROW);
					}
				}
				break;
			}
			case 'y': return checkKeyword(scanner, 2, 4, "peof", TOKEN_TYPE_OF);
			case 'r': return checkKeyword(scanner, 2, 2, "ue", TOKEN_TRUE);
			}
		}
		break;
	}
	case 'v': return checkKeyword(scanner, 1, 2, "ar", TOKEN_VAR);
	case 'w': return checkKeyword(scanner, 1, 4, "hile", TOKEN_WHILE);
	}

	return TOKEN_IDENTIFIER;
}

static TokenType checkModule(Scanner* scanner, uint32_t start, uint32_t length, C_STR rest, TokenType type) {
	if (scanner->current - scanner->start == start + length &&
		memcmp(scanner->start + start, rest, length) == 0) {
		return type;
	}

	return TOKEN_NIL;
}

//convert
static TokenType builtinType(Scanner* scanner) {
	if ((scanner->start[0] == '@') && (scanner->current - scanner->start > 1)) {
		switch (scanner->start[1])
		{
		case 'm':return checkModule(scanner, 2, 3, "ath", TOKEN_MODULE_MATH);
		case 'a':return checkModule(scanner, 2, 4, "rray", TOKEN_MODULE_ARRAY);
		case 'o':return checkModule(scanner, 2, 5, "bject", TOKEN_MODULE_OBJECT);
		case 's': {
			if (scanner->current - scanner->start > 2) {
				switch (scanner->start[2])
				{
				case 't':return checkModule(scanner, 3, 4, "ring", TOKEN_MODULE_STRING);
				case 'y':return checkModule(scanner, 3, 1, "s", TOKEN_MODULE_SYSTEM);
				}
			}
			break;
		}
		case 'c':return checkModule(scanner, 2, 3, "tor", TOKEN_MODULE_CTOR);
		case 't':return checkModule(scanner, 2, 3, "ime", TOKEN_MODULE_TIME);
		}
	}

	return TOKEN_NIL;
}

static Token mention(Scanner* scanner) {
	while (isAlpha(peek(scanner)) || isDigit(peek(scanner))) advance(scanner);

	TokenType type = builtinType(scanner);

	if (type == TOKEN_NIL) {
		return errorToken(scanner, "Unexpected module (Available modules : @math, @array, @object, @string, @time, @ctor, @sys).");
	}

	return makeToken(scanner, type);
}

static Token identifier(Scanner* scanner) {
	while (isAlpha(peek(scanner)) || isDigit(peek(scanner))) advance(scanner);
	return makeToken(scanner, identifierType(scanner));
}

static bool isBinDigit(char c) {
	return c == '0' || c == '1';
}

static bool isHexDigit(char c) {
	return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static Token number(Scanner* scanner) {
	switch (peek(scanner))
	{
	case 'b':
	case 'B':
		advance(scanner); // Skip '0'
		advance(scanner); // Skip 'b' or 'B'

		if (!isBinDigit(peek(scanner))) {
			return errorToken(scanner, "Invalid bin number format.");
		}

		do {
			advance(scanner);
		} while (isBinDigit(peek(scanner)));

		return makeToken(scanner, TOKEN_NUMBER_BIN);
	case 'x':
	case 'X':
		advance(scanner); // Skip '0'
		advance(scanner); // Skip 'x' or 'X'

		if (!isHexDigit(peek(scanner))) {
			return errorToken(scanner, "Invalid hex number format.");
		}

		do {
			advance(scanner);
		} while (isHexDigit(peek(scanner)));

		return makeToken(scanner, TOKEN_NUMBER_HEX);
	default:
		break;
	}

	// Parse the number based on its base.
	// Decimal number.
	while (isDigit(peek(scanner))) {
		advance(scanner);
	}

	// Look for a fractional part.
	if (peek(scanner) == '.' && isDigit(peekNext(scanner))) {
		// Consume the ".".
		advance(scanner);

		while (isDigit(peek(scanner))) {
			advance(scanner);
		}
	}

	// Look for scientific notation (e or E).
	if (peek(scanner) == 'e' || peek(scanner) == 'E') {
		// Consume the 'e' or 'E'.
		advance(scanner);

		// Check for an optional sign (+ or -) in the exponent.
		if (peek(scanner) == '+' || peek(scanner) == '-') {
			advance(scanner); // Consume the sign.
		}

		// Ensure there are digits in the exponent.
		if (!isDigit(peek(scanner))) {
			return errorToken(scanner, "Expected digit after 'e' or 'E'.");
		}

		// Parse the exponent digits.
		while (isDigit(peek(scanner))) {
			advance(scanner);
		}
	}

	return makeToken(scanner, TOKEN_NUMBER);
}

static Token string(Scanner* scanner) {
	bool isEscapeString = false;

	for (char c = '\0'; (c = peek(scanner)) != '"' && !isAtEnd(scanner); advance(scanner)) {
		switch (c)
		{
		case '\n':
			scanner->line++;
			break;
		case '\\': {
			advance(scanner); // skip "\\"

			//the backslash was the last source character: the escape pair can
			//never complete and the loop's advance(scanner) would step past the
			//terminator (1-byte out-of-bounds read) -> unterminated string
			if (isAtEnd(scanner)) return errorToken(scanner, "Unterminated string.");

			if (!isEscapeString) {
				switch (peek(scanner)) {
				case '"':  // '\"'
				case '\\': // '\\'
				case 'n':  // '\n'
				case 't':  // '\t'
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

	if (isAtEnd(scanner)) return errorToken(scanner, "Unterminated string.");

	// The closing quote.
	advance(scanner);
	return makeToken(scanner, isEscapeString ? TOKEN_STRING_ESCAPE : TOKEN_STRING);
}

Token scanToken(Scanner* scanner)
{
	switch (skipWhitespace(scanner))
	{
	case TOKEN_UNTERMINATED_COMMENT:
		return errorToken(scanner, "Expect '*/' after comment.");
	default:
		break;
	}

	scanner->start = scanner->current;

	if (isAtEnd(scanner)) return makeToken(scanner, TOKEN_EOF);

	char c = advance(scanner);

	if (isAlpha(c)) return identifier(scanner);
	if (isDigit(c)) return number(scanner);

	switch (c) {
	case '(': return makeToken(scanner, TOKEN_LEFT_PAREN);
	case ')': return makeToken(scanner, TOKEN_RIGHT_PAREN);
	case '{': return makeToken(scanner, TOKEN_LEFT_BRACE);
	case '}': return makeToken(scanner, TOKEN_RIGHT_BRACE);
	case '[': return makeToken(scanner, TOKEN_LEFT_SQUARE_BRACKET);
	case ']': return makeToken(scanner, TOKEN_RIGHT_SQUARE_BRACKET);
	case ';': return makeToken(scanner, TOKEN_SEMICOLON);
	case ':': return makeToken(scanner, TOKEN_COLON);
	case ',': return makeToken(scanner, TOKEN_COMMA);
	case '.': return makeToken(scanner, TOKEN_DOT);
	case '-': return makeToken(scanner, TOKEN_MINUS);
	case '+': return makeToken(scanner, TOKEN_PLUS);
	case '/': return makeToken(scanner, TOKEN_SLASH);
	case '*': return makeToken(scanner, TOKEN_STAR);
	case '%': return makeToken(scanner, TOKEN_PERCENT);
	case '@':
		return isAlpha(peek(scanner)) ? mention(scanner) : errorToken(scanner, "Expected module name after @.");
	case '!':
		return makeToken(scanner, 
			match(scanner, '=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
	case '=': {
		if (match(scanner, '=')) {
			return makeToken(scanner, TOKEN_EQUAL_EQUAL);
		}
		else if (match(scanner, '>')) {
			return makeToken(scanner, TOKEN_RIGHT_ARROW);
		}
		else {
			return makeToken(scanner, TOKEN_EQUAL);
		}
	}
	case '<': {
		if (!match(scanner, '<')) {
			return makeToken(scanner, match(scanner, '=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
		}
		else {
			return makeToken(scanner, TOKEN_BIT_SHL);
		}
	}
	case '>': {
		if (!match(scanner, '>')) {
			return makeToken(scanner, match(scanner, '=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
		}
		else {
			return makeToken(scanner, match(scanner, '>') ? TOKEN_BIT_SHR : TOKEN_BIT_SAR);
		}
	}
	case '&': return makeToken(scanner, TOKEN_BIT_AND);
	case '|': return makeToken(scanner, TOKEN_BIT_OR);
	case '~': return makeToken(scanner, TOKEN_BIT_NOT);
	case '^': return makeToken(scanner, TOKEN_BIT_XOR);
	case '"': return string(scanner);
	}

	if (!isAtEnd(scanner)) {
		advance(scanner);//stop deadly looping
		return errorToken(scanner, "Unexpected character. ");
	}
	else {
		return makeToken(scanner, TOKEN_EOF);
	}
}