/*
 * MIT License
 * Copyright (c) 2025 IMSDCrueoft
 * LoxFlux language server - LoxFlux lexer.
 * Ported from LoxFlux src/scanner.c for editor tooling.
 */

export enum TokenType {
	// Single-character tokens.
	LParen, RParen,
	LBrace, RBrace,
	LBracket, RBracket,
	Comma, Dot, Minus, Plus,
	Semicolon, Slash, Star, Percent, Colon,
	// Bit-calc
	BitAnd, BitOr, BitXor, BitNot, BitShl, BitSar, BitShr,
	// One or two character tokens.
	Bang, BangEqual,
	Equal, EqualEqual,
	Greater, GreaterEqual,
	Less, LessEqual,
	// Literals.
	Identifier, String, StringEscape, Number, NumberBin, NumberHex,
	// Builtin Literals.
	ModuleMath, ModuleArray, ModuleObject, ModuleString, ModuleTime, ModuleCtor, ModuleSys,
	// Keywords.
	And, Class, Else, False,
	For, Fun, If, Nil, Or,
	Print, Return, Super, This,
	True, Var, While, Do, Const,
	Break, Continue, Throw, Lambda, RightArrow,
	Branch, None, InstanceOf, TypeOf,
	Import, Export,

	Error, EOF,
}

export interface Token {
	type: TokenType;
	/** start offset in source */
	start: number;
	/** end offset (exclusive) in source */
	end: number;
	/** raw text */
	text: string;
	/** decoded value for strings (without quotes) */
	value?: string;
	/** 0-based line of token start */
	line: number;
	/** 0-based character (UTF-16 code unit) of token start */
	character: number;
	/** parse-time error message when type === Error */
	errorMessage?: string;
}

export interface Comment {
	/** start offset in source */
	start: number;
	/** end offset (exclusive) in source */
	end: number;
	/** 0-based line of comment start */
	line: number;
	/** true for `// ...` line comments */
	isLine: boolean;
	text: string;
}

const KEYWORDS: Record<string, TokenType> = {
	'and': TokenType.And,
	'break': TokenType.Break,
	'branch': TokenType.Branch,
	'class': TokenType.Class,
	'const': TokenType.Const,
	'continue': TokenType.Continue,
	'do': TokenType.Do,
	'else': TokenType.Else,
	'export': TokenType.Export,
	'false': TokenType.False,
	'for': TokenType.For,
	'fun': TokenType.Fun,
	'if': TokenType.If,
	'import': TokenType.Import,
	'instanceof': TokenType.InstanceOf,
	'lambda': TokenType.Lambda,
	'nil': TokenType.Nil,
	'none': TokenType.None,
	'or': TokenType.Or,
	'print': TokenType.Print,
	'return': TokenType.Return,
	'super': TokenType.Super,
	'this': TokenType.This,
	'throw': TokenType.Throw,
	'true': TokenType.True,
	'typeof': TokenType.TypeOf,
	'var': TokenType.Var,
	'while': TokenType.While,
};

const MODULES: Record<string, TokenType> = {
	'math': TokenType.ModuleMath,
	'array': TokenType.ModuleArray,
	'object': TokenType.ModuleObject,
	'string': TokenType.ModuleString,
	'time': TokenType.ModuleTime,
	'ctor': TokenType.ModuleCtor,
	'sys': TokenType.ModuleSys,
};

function isAlpha(c: string): boolean {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c === '_';
}

function isDigit(c: string): boolean {
	return c >= '0' && c <= '9';
}

function isBinDigit(c: string): boolean {
	return c === '0' || c === '1';
}

function isHexDigit(c: string): boolean {
	return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

export interface LexResult {
	tokens: Token[];
	comments: Comment[];
}

/**
 * Tokenize LoxFlux source text.
 * Positions are 0-based and expressed in UTF-16 code units to match LSP.
 */
export function lex(source: string): LexResult {
	const tokens: Token[] = [];
	const comments: Comment[] = [];

	let pos = 0;
	let line = 0;
	let done = false;

	const isAtEnd = () => pos >= source.length;
	const peek = () => (isAtEnd() ? '\0' : source[pos]);
	const peekNext = () => (pos + 1 >= source.length ? '\0' : source[pos + 1]);

	function advance(): string {
		const c = source[pos++];
		if (c === '\n') {
			line++;
		}
		return c;
	}

	function match(expected: string): boolean {
		if (isAtEnd() || source[pos] !== expected) {
			return false;
		}
		advance();
		return true;
	}

	function pushToken(type: TokenType, start: number, startLine: number, startChar: number, value?: string): Token {
		const token: Token = {
			type,
			start,
			end: pos,
			text: source.slice(start, pos),
			value,
			line: startLine,
			character: startChar,
		};
		tokens.push(token);
		return token;
	}

	function pushErrorToken(message: string, start: number, startLine: number, startChar: number): Token {
		const token = pushToken(TokenType.Error, start, startLine, startChar);
		token.errorMessage = message;
		return token;
	}

	function pushEofToken(start: number, startLine: number, startChar: number): void {
		tokens.push({
			type: TokenType.EOF,
			start,
			end: start,
			text: '',
			line: startLine,
			character: startChar,
		});
	}

	function lineStartOf(offset: number): number {
		let i = offset;
		while (i > 0 && source[i - 1] !== '\n') {
			i--;
		}
		return i;
	}

	/** Re-run a token producer that expects the first char to be already consumed. */
	function retake(start: number, startLine: number, producer: () => void): void {
		pos = start + 1;
		line = startLine;
		producer();
	}

	/** Returns false when a block comment is unterminated. */
	function skipWhitespace(): boolean {
		while (true) {
			const c = peek();
			switch (c) {
				case ' ':
				case '\r':
				case '\t':
				case '\n':
					advance();
					break;
				case '/': {
					if (peekNext() === '/') {
						const start = pos;
						const startLine = line;
						advance(); // '/'
						while (peek() !== '\n' && !isAtEnd()) {
							advance();
						}
						comments.push({ start, end: pos, line: startLine, isLine: true, text: source.slice(start, pos) });
						break;
					} else if (peekNext() === '*') {
						const start = pos;
						const startLine = line;
						advance(); // '/'
						advance(); // '*'
						while (true) {
							while (peek() !== '*' && !isAtEnd()) {
								advance();
							}
							if (isAtEnd()) {
								return false;
							}
							advance(); // '*'
							if (isAtEnd()) {
								return false;
							}
							if (peek() === '/') {
								advance();
								break;
							}
						}
						comments.push({ start, end: pos, line: startLine, isLine: false, text: source.slice(start, pos) });
						break;
					}
					return true;
				}
				default:
					return true;
			}
		}
	}

	function stringToken(): void {
		const start = pos - 1; // opening quote
		const startLine = line;
		const startChar = start - lineStartOf(start);

		let isEscapeString = false;
		while (peek() !== '"' && !isAtEnd()) {
			const c = peek();
			if (c === '\\') {
				advance(); // skip '\'
				if (isAtEnd()) {
					break;
				}
				const escaped = peek();
				if (escaped === '"' || escaped === '\\' || escaped === 'n') {
					isEscapeString = true;
				}
			}
			advance();
		}

		if (isAtEnd()) {
			pushErrorToken('Unterminated string.', start, startLine, startChar);
			return;
		}

		// consume closing quote
		advance();

		const raw = source.slice(start + 1, pos - 1);
		const value = isEscapeString ? decodeStringEscapes(raw) : raw;
		pushToken(isEscapeString ? TokenType.StringEscape : TokenType.String, start, startLine, startChar, value);
	}

	function numberToken(): void {
		const start = pos - 1;
		const startLine = line;
		const startChar = start - lineStartOf(start);

		const c = peek();
		if (c === 'b' || c === 'B') {
			advance(); // 'b'
			if (!isBinDigit(peek())) {
				pushErrorToken('Invalid bin number format.', start, startLine, startChar);
				return;
			}
			do {
				advance();
			} while (isBinDigit(peek()));
			pushToken(TokenType.NumberBin, start, startLine, startChar);
			return;
		}
		if (c === 'x' || c === 'X') {
			advance(); // 'x'
			if (!isHexDigit(peek())) {
				pushErrorToken('Invalid hex number format.', start, startLine, startChar);
				return;
			}
			do {
				advance();
			} while (isHexDigit(peek()));
			pushToken(TokenType.NumberHex, start, startLine, startChar);
			return;
		}

		while (isDigit(peek())) {
			advance();
		}

		// fractional part
		if (peek() === '.' && isDigit(peekNext())) {
			advance();
			while (isDigit(peek())) {
				advance();
			}
		}

		// scientific notation
		if (peek() === 'e' || peek() === 'E') {
			advance();
			if (peek() === '+' || peek() === '-') {
				advance();
			}
			if (!isDigit(peek())) {
				pushErrorToken("Expected digit after 'e' or 'E'.", start, startLine, startChar);
				return;
			}
			while (isDigit(peek())) {
				advance();
			}
		}

		pushToken(TokenType.Number, start, startLine, startChar);
	}

	function identifierToken(): void {
		const start = pos - 1;
		const startLine = line;
		const startChar = start - lineStartOf(start);

		while (isAlpha(peek()) || isDigit(peek())) {
			advance();
		}

		const text = source.slice(start, pos);
		const keywordType = KEYWORDS[text];
		pushToken(keywordType !== undefined ? keywordType : TokenType.Identifier, start, startLine, startChar);
	}

	function moduleToken(): void {
		const start = pos - 1; // '@'
		const startLine = line;
		const startChar = start - lineStartOf(start);

		while (isAlpha(peek()) || isDigit(peek())) {
			advance();
		}

		const name = source.slice(start + 1, pos);
		const type = MODULES[name];
		if (type === undefined) {
			pushErrorToken(
				'Unexpected module (Available modules : @math, @array, @object, @string, @time, @ctor, @sys).',
				start, startLine, startChar,
			);
			return;
		}
		pushToken(type, start, startLine, startChar);
	}

	while (!done) {
		if (!skipWhitespace()) {
			pushErrorToken("Expect '*/' after comment.", pos, line, pos - lineStartOf(pos));
			break;
		}

		if (isAtEnd()) {
			pushEofToken(pos, line, pos - lineStartOf(pos));
			break;
		}

		const start = pos;
		const startLine = line;
		const startChar = pos - lineStartOf(pos);
		const c = advance();

		if (isAlpha(c)) {
			retake(start, startLine, identifierToken);
		} else if (isDigit(c)) {
			retake(start, startLine, numberToken);
		} else {
			switch (c) {
				case '(': pushToken(TokenType.LParen, start, startLine, startChar); break;
				case ')': pushToken(TokenType.RParen, start, startLine, startChar); break;
				case '{': pushToken(TokenType.LBrace, start, startLine, startChar); break;
				case '}': pushToken(TokenType.RBrace, start, startLine, startChar); break;
				case '[': pushToken(TokenType.LBracket, start, startLine, startChar); break;
				case ']': pushToken(TokenType.RBracket, start, startLine, startChar); break;
				case ';': pushToken(TokenType.Semicolon, start, startLine, startChar); break;
				case ':': pushToken(TokenType.Colon, start, startLine, startChar); break;
				case ',': pushToken(TokenType.Comma, start, startLine, startChar); break;
				case '.': pushToken(TokenType.Dot, start, startLine, startChar); break;
				case '-': pushToken(TokenType.Minus, start, startLine, startChar); break;
				case '+': pushToken(TokenType.Plus, start, startLine, startChar); break;
				case '/': pushToken(TokenType.Slash, start, startLine, startChar); break;
				case '*': pushToken(TokenType.Star, start, startLine, startChar); break;
				case '%': pushToken(TokenType.Percent, start, startLine, startChar); break;
				case '@':
					if (isAlpha(peek())) {
						retake(start, startLine, moduleToken);
					} else {
						pushErrorToken('Expected module name after @.', start, startLine, startChar);
					}
					break;
				case '!':
					pushToken(match('=') ? TokenType.BangEqual : TokenType.Bang, start, startLine, startChar);
					break;
				case '=':
					if (match('=')) {
						pushToken(TokenType.EqualEqual, start, startLine, startChar);
					} else if (match('>')) {
						pushToken(TokenType.RightArrow, start, startLine, startChar);
					} else {
						pushToken(TokenType.Equal, start, startLine, startChar);
					}
					break;
				case '<':
					if (!match('<')) {
						pushToken(match('=') ? TokenType.LessEqual : TokenType.Less, start, startLine, startChar);
					} else {
						pushToken(TokenType.BitShl, start, startLine, startChar);
					}
					break;
				case '>':
					if (!match('>')) {
						pushToken(match('=') ? TokenType.GreaterEqual : TokenType.Greater, start, startLine, startChar);
					} else {
						pushToken(match('>') ? TokenType.BitShr : TokenType.BitSar, start, startLine, startChar);
					}
					break;
				case '&': pushToken(TokenType.BitAnd, start, startLine, startChar); break;
				case '|': pushToken(TokenType.BitOr, start, startLine, startChar); break;
				case '~': pushToken(TokenType.BitNot, start, startLine, startChar); break;
				case '^': pushToken(TokenType.BitXor, start, startLine, startChar); break;
				case '"':
					retake(start, startLine, stringToken);
					break;
				default:
					if (pos < source.length) {
						pushErrorToken('Unexpected character.', start, startLine, startChar);
					} else {
						// trailing unknown character: emit EOF like the C scanner
						pushEofToken(start, startLine, startChar);
						done = true;
					}
					break;
			}
		}
	}

	return { tokens, comments };
}

/** Decode LoxFlux string escapes: \" \\ \n (others are left as-is). */
function decodeStringEscapes(raw: string): string {
	let out = '';
	for (let i = 0; i < raw.length; i++) {
		const c = raw[i];
		if (c === '\\' && i + 1 < raw.length) {
			const n = raw[i + 1];
			if (n === '"' || n === '\\') {
				out += n;
				i++;
				continue;
			}
			if (n === 'n') {
				out += '\n';
				i++;
				continue;
			}
		}
		out += c;
	}
	return out;
}
