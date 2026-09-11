/*
 * MIT License
 * Copyright (c) 2025 IMSDCrueoft
 * LoxFlux language server - recursive descent parser.
 * Grammar ported from LoxFlux src/compiler.c.
 */

import { Token, TokenType } from './lexer';
import {
	BlockStmt, BranchCase, Expr, FunDeclStmt, ObjectProperty, ParseError, Script, Stmt, VarDeclarator,
} from './ast';

enum Precedence {
	PrecNone,
	PrecAssignment, // =
	PrecOr, // or
	PrecAnd, // and
	PrecBitwise, // | ^ & << >> >>> (shared level, like the compiler)
	PrecEquality, // == !=
	PrecInstanceof, // instanceOf
	PrecComparison, // < > <= >=
	PrecTerm, // + -
	PrecFactor, // * / %
	PrecUnary, // ! - ~ typeof
	PrecCall, // . () []
	PrecOperate,
	PrecPrimary,
}

const EOF_TOKEN: Token = {
	type: TokenType.EOF, start: 0, end: 0, text: '', line: 0, character: 0,
};

type PrefixFn = () => Expr;
type InfixFn = (left: Expr, canAssign: boolean) => Expr;

interface Rule {
	prefix: PrefixFn | undefined;
	infix: InfixFn | undefined;
	precedence: Precedence;
}

export class Parser {
	private tokens: Token[];
	private current = 0;
	private errors: ParseError[] = [];
	private panicMode = false;

	constructor(tokens: Token[]) {
		this.tokens = tokens;
	}

	parse(): Script {
		const statements: Stmt[] = [];
		while (!this.check(TokenType.EOF)) {
			const decl = this.declaration();
			if (decl) {
				statements.push(decl);
			}
		}
		const end = this.tokens.length > 0 ? this.tokens[this.tokens.length - 1].end : 0;
		return {
			kind: 'Script',
			start: 0,
			end,
			statements,
			tokens: this.tokens,
			comments: [],
			errors: this.errors,
		};
	}

	// ---- token helpers -------------------------------------------------

	private get previous(): Token {
		return this.tokens[this.current - 1] ?? EOF_TOKEN;
	}

	private get peek(): Token {
		return this.tokens[this.current] ?? EOF_TOKEN;
	}

	private check(type: TokenType): boolean {
		return this.peek.type === type;
	}

	private advance(): Token {
		if (!this.check(TokenType.EOF)) {
			this.current++;
		}
		return this.previous;
	}

	private match(type: TokenType): boolean {
		if (!this.check(type)) {
			return false;
		}
		this.advance();
		return true;
	}

	private consume(type: TokenType, message: string): Token {
		if (this.check(type)) {
			return this.advance();
		}
		this.errorAtCurrent(message);
		return this.peek;
	}

	private errorAtCurrent(message: string): void {
		if (this.panicMode) {
			return;
		}
		this.panicMode = true;
		const t = this.peek;
		this.errors.push({
			message,
			start: t.start,
			end: t.end > t.start ? t.end : t.start + 1,
		});
	}

	private error(message: string, token: Token): void {
		if (this.panicMode) {
			return;
		}
		this.panicMode = true;
		this.errors.push({
			message,
			start: token.start,
			end: token.end > token.start ? token.end : token.start + 1,
		});
	}

	private synchronize(): void {
		this.panicMode = false;
		while (this.peek.type !== TokenType.EOF) {
			if (this.previous.type === TokenType.Semicolon) {
				return;
			}
			switch (this.peek.type) {
				case TokenType.Class:
				case TokenType.Fun:
				case TokenType.Var:
				case TokenType.Const:
				case TokenType.For:
				case TokenType.If:
				case TokenType.Branch:
				case TokenType.Do:
				case TokenType.While:
				case TokenType.Print:
				case TokenType.Return:
				case TokenType.Throw:
					return;
				default:
					this.advance();
					break;
			}
		}
	}

	// ---- declarations --------------------------------------------------

	private declaration(): Stmt | undefined {
		if (this.panicMode) {
			this.synchronize();
		}

		let stmt: Stmt;
		if (this.match(TokenType.Class)) {
			stmt = this.classDeclaration();
		} else if (this.match(TokenType.Fun)) {
			stmt = this.funDeclaration();
		} else if (this.match(TokenType.Var)) {
			stmt = this.varDeclaration(false);
		} else if (this.match(TokenType.Const)) {
			stmt = this.varDeclaration(true);
		} else {
			stmt = this.statement();
		}

		if (this.panicMode) {
			this.synchronize();
		}
		return stmt;
	}

	private varDeclaration(isConst: boolean): Stmt {
		const declToken = this.previous;
		const declarators: VarDeclarator[] = [];

		do {
			const name = this.consume(TokenType.Identifier, isConst ? 'Expect constant name.' : 'Expect variable name.');

			let initializer: Expr | undefined;
			if (this.match(TokenType.Equal)) {
				initializer = this.expression();
			} else if (isConst) {
				this.errorAtCurrent('Constant must be initialized.');
			}

			declarators.push({ name, initializer });

			if (this.panicMode) {
				break;
			}
		} while (this.match(TokenType.Comma));

		this.consume(TokenType.Semicolon, isConst ? "Expect ';' after constant declaration." : "Expect ';' after variable declaration.");

		return {
			kind: 'VarDecl',
			start: declToken.start,
			end: this.previous.end,
			declToken,
			isConst,
			declarators,
		};
	}

	private funDeclaration(): Stmt {
		const funToken = this.previous;
		const name = this.consume(TokenType.Identifier, 'Expect function name.');
		const body = this.functionBody();
		return {
			kind: 'FunDecl',
			start: funToken.start,
			end: body.block.end,
			funToken,
			name,
			params: body.params,
			body: body.block,
		};
	}

	private classDeclaration(): Stmt {
		const classToken = this.previous;
		const name = this.consume(TokenType.Identifier, 'Expect class name.');

		let superclass: Expr | undefined;
		if (this.match(TokenType.Less)) {
			superclass = {
				kind: 'Identifier',
				start: this.peek.start,
				end: this.peek.end,
				token: this.consume(TokenType.Identifier, 'Expect superclass name.'),
			};
			if (superclass.token.text === name.text) {
				this.error("A class can't inherit from itself.", superclass.token);
			}
		}

		this.consume(TokenType.LBrace, "Expect '{' before class body.");

		const methods: FunDeclStmt[] = [];
		while (!this.check(TokenType.RBrace) && !this.check(TokenType.EOF)) {
			const methodToken = this.consume(TokenType.Identifier, 'Expect method name.');
			const body = this.functionBody();
			methods.push({
				kind: 'FunDecl',
				start: methodToken.start,
				end: body.block.end,
				funToken: methodToken,
				name: methodToken,
				params: body.params,
				body: body.block,
			});
		}

		this.consume(TokenType.RBrace, "Expect '}' after class body.");

		return {
			kind: 'ClassDecl',
			start: classToken.start,
			end: this.previous.end,
			classToken,
			name,
			superclass: superclass as never,
			methods,
		};
	}

	private functionBody(): { params: Token[]; block: BlockStmt } {
		this.consume(TokenType.LParen, "Expect '(' before function parameters.");

		const params: Token[] = [];
		if (!this.check(TokenType.RParen)) {
			do {
				if (params.length >= 255) {
					this.errorAtCurrent("Can't have more than 255 parameters.");
				}
				params.push(this.consume(TokenType.Identifier, 'Expect parameter name.'));
			} while (this.match(TokenType.Comma));
		}

		this.consume(TokenType.RParen, "Expect ')' after function parameters.");

		if (this.match(TokenType.RightArrow)) {
			this.errorAtCurrent("'=>' can only be used after lambda parameters.");
			const expr = this.expression();
			const block: BlockStmt = {
				kind: 'Block',
				start: expr.start,
				end: expr.end,
				lbrace: this.peek,
				statements: [],
			};
			return { params, block };
		}

		this.consume(TokenType.LBrace, "Expect '{' before function body.");
		const block = this.block();
		return { params, block };
	}

	private block(): BlockStmt {
		const lbrace = this.previous;
		const statements: Stmt[] = [];
		while (!this.check(TokenType.RBrace) && !this.check(TokenType.EOF)) {
			const decl = this.declaration();
			if (decl) {
				statements.push(decl);
			}
		}
		this.consume(TokenType.RBrace, "Expect '}' after block.");
		return {
			kind: 'Block',
			start: lbrace.start,
			end: this.previous.end,
			lbrace,
			statements,
		};
	}

	// ---- statements ----------------------------------------------------

	private statement(): Stmt {
		if (this.match(TokenType.Print)) {
			return this.printStatement();
		}
		if (this.match(TokenType.If)) {
			return this.ifStatement();
		}
		if (this.match(TokenType.Branch)) {
			return this.branchStatement();
		}
		if (this.match(TokenType.Return)) {
			return this.returnStatement();
		}
		if (this.match(TokenType.While)) {
			return this.whileStatement();
		}
		if (this.match(TokenType.Do)) {
			return this.doWhileStatement();
		}
		if (this.match(TokenType.For)) {
			return this.forStatement();
		}
		if (this.match(TokenType.Break)) {
			const token = this.previous;
			this.consume(TokenType.Semicolon, "Expect ';' after 'break'.");
			return { kind: 'Break', start: token.start, end: this.previous.end, token };
		}
		if (this.match(TokenType.Continue)) {
			const token = this.previous;
			this.consume(TokenType.Semicolon, "Expect ';' after 'continue'.");
			return { kind: 'Continue', start: token.start, end: this.previous.end, token };
		}
		if (this.match(TokenType.LBrace)) {
			return this.block();
		}
		if (this.match(TokenType.Throw)) {
			return this.throwStatement();
		}
		if (this.match(TokenType.Export)) {
			return this.exportStatement();
		}
		return this.expressionStatement();
	}

	private printStatement(): Stmt {
		const printToken = this.previous;
		const expression = this.expression();
		this.consume(TokenType.Semicolon, "Expect ';' after value.");
		return { kind: 'Print', start: printToken.start, end: this.previous.end, printToken, expression };
	}

	private throwStatement(): Stmt {
		const throwToken = this.previous;
		const value = this.expression();
		this.consume(TokenType.Semicolon, "Expect ';' after value.");
		return { kind: 'Throw', start: throwToken.start, end: this.previous.end, throwToken, value };
	}

	private returnStatement(): Stmt {
		const returnToken = this.previous;
		let value: Expr | undefined;
		if (!this.match(TokenType.Semicolon)) {
			value = this.expression();
			this.consume(TokenType.Semicolon, "Expect ';' after return value.");
		}
		return { kind: 'Return', start: returnToken.start, end: this.previous.end, returnToken, value };
	}

	private exportStatement(): Stmt {
		const exportToken = this.previous;
		let value: Expr | undefined;
		if (!this.match(TokenType.Semicolon)) {
			value = this.expression();
			this.consume(TokenType.Semicolon, "Expect ';' after export value.");
		}
		return { kind: 'Export', start: exportToken.start, end: this.previous.end, exportToken, value };
	}

	private ifStatement(): Stmt {
		const ifToken = this.previous;
		this.consume(TokenType.LParen, "Expect '(' after 'if'.");
		const condition = this.expression();
		this.consume(TokenType.RParen, "Expect ')' after condition.");
		const thenBranch = this.statement();
		let elseBranch: Stmt | undefined;
		if (this.match(TokenType.Else)) {
			elseBranch = this.statement();
		}
		return {
			kind: 'If',
			start: ifToken.start,
			end: this.previous.end,
			ifToken,
			condition,
			thenBranch,
			elseBranch,
		};
	}

	private whileStatement(): Stmt {
		const whileToken = this.previous;
		this.consume(TokenType.LParen, "Expect '(' after 'while'.");
		const condition = this.expression();
		this.consume(TokenType.RParen, "Expect ')' after condition.");
		const body = this.statement();
		return { kind: 'While', start: whileToken.start, end: this.previous.end, whileToken, condition, body };
	}

	private doWhileStatement(): Stmt {
		const doToken = this.previous;
		const body = this.statement();
		this.consume(TokenType.While, "Expect 'while' after 'do' to form a valid 'do-while'.");
		this.consume(TokenType.LParen, "Expect '(' after 'while'.");
		const condition = this.expression();
		this.consume(TokenType.RParen, "Expect ')' after condition.");
		this.consume(TokenType.Semicolon, "Expect ';' after 'do-while' loop.");
		return { kind: 'DoWhile', start: doToken.start, end: this.previous.end, doToken, body, condition };
	}

	private forStatement(): Stmt {
		const forToken = this.previous;
		this.consume(TokenType.LParen, "Expect '(' after 'for'.");

		let initializer: Stmt | undefined;
		if (this.match(TokenType.Semicolon)) {
			// no initializer
		} else if (this.match(TokenType.Var)) {
			initializer = this.varDeclaration(false);
		} else if (this.match(TokenType.Const)) {
			initializer = this.varDeclaration(true);
		} else {
			initializer = this.expressionStatement();
		}

		let condition: Expr | undefined;
		if (!this.match(TokenType.Semicolon)) {
			condition = this.expression();
			this.consume(TokenType.Semicolon, "Expect ';' after loop condition.");
		}

		let increment: Expr | undefined;
		if (!this.match(TokenType.RParen)) {
			increment = this.expression();
			this.consume(TokenType.RParen, "Expect ')' after for clauses.");
		}

		const body = this.statement();
		return {
			kind: 'For',
			start: forToken.start,
			end: this.previous.end,
			forToken,
			initializer: initializer as never,
			condition,
			increment,
			body,
		};
	}

	private branchStatement(): Stmt {
		const branchToken = this.previous;
		this.consume(TokenType.LBrace, "Expect '{' after 'branch'.");

		const cases: BranchCase[] = [];
		let closed = false;
		while (!this.check(TokenType.RBrace) && !this.check(TokenType.EOF)) {
			if (this.match(TokenType.None)) {
				const noneToken = this.previous;
				const colon = this.consume(TokenType.Colon, "Expect ':' after 'none'.");
				const body = this.statement();
				cases.push({ noneToken, condition: undefined, colon, body });
				// 'none' must be the last case
				this.consume(TokenType.RBrace, "Expect '}' after 'none' case.");
				closed = true;
				break;
			}

			const condition = this.expression();
			const colon = this.consume(TokenType.Colon, "Expect ':' after condition.");
			const body = this.statement();
			cases.push({ noneToken: undefined, condition, colon, body });
		}

		if (cases.length === 0) {
			this.errorAtCurrent("Expect at least one case in 'branch'.");
		}

		if (!closed) {
			this.consume(TokenType.RBrace, "Expect '}' after 'branch' body.");
		}
		return { kind: 'Branch', start: branchToken.start, end: this.previous.end, branchToken, cases };
	}

	private expressionStatement(): Stmt {
		const expression = this.expression();
		this.consume(TokenType.Semicolon, "Expect ';' after expression.");
		return { kind: 'ExprStmt', start: expression.start, end: this.previous.end, expression };
	}

	// ---- expressions ---------------------------------------------------

	private expression(): Expr {
		return this.parsePrecedence(Precedence.PrecAssignment);
	}

	private getRule(type: TokenType): Rule {
		switch (type) {
			case TokenType.LParen:
				return { prefix: () => this.grouping(), infix: (l) => this.call(l), precedence: Precedence.PrecCall };
			case TokenType.LBracket:
				return { prefix: () => this.arrayLiteral(), infix: (l, c) => this.subscript(l, c), precedence: Precedence.PrecCall };
			case TokenType.Dot:
				return { prefix: undefined, infix: (l, c) => this.dot(l, c), precedence: Precedence.PrecCall };
			case TokenType.Minus:
				return { prefix: () => this.unary(), infix: (l) => this.binary(l), precedence: Precedence.PrecTerm };
			case TokenType.Plus:
				return { prefix: undefined, infix: (l) => this.binary(l), precedence: Precedence.PrecTerm };
			case TokenType.Star:
			case TokenType.Slash:
			case TokenType.Percent:
				return { prefix: undefined, infix: (l) => this.binary(l), precedence: Precedence.PrecFactor };
			case TokenType.BangEqual:
			case TokenType.EqualEqual:
				return { prefix: undefined, infix: (l) => this.binary(l), precedence: Precedence.PrecEquality };
			case TokenType.InstanceOf:
				return { prefix: undefined, infix: (l) => this.binary(l), precedence: Precedence.PrecInstanceof };
			case TokenType.Greater:
			case TokenType.GreaterEqual:
			case TokenType.Less:
			case TokenType.LessEqual:
				return { prefix: undefined, infix: (l) => this.binary(l), precedence: Precedence.PrecComparison };
			case TokenType.BitAnd:
			case TokenType.BitOr:
			case TokenType.BitXor:
			case TokenType.BitShl:
			case TokenType.BitSar:
			case TokenType.BitShr:
				return { prefix: undefined, infix: (l) => this.binary(l), precedence: Precedence.PrecBitwise };
			case TokenType.And:
				return { prefix: undefined, infix: (l) => this.logical(l, 'and'), precedence: Precedence.PrecAnd };
			case TokenType.Or:
				return { prefix: undefined, infix: (l) => this.logical(l, 'or'), precedence: Precedence.PrecOr };
			case TokenType.Bang:
			case TokenType.BitNot:
			case TokenType.TypeOf:
				return { prefix: () => this.unary(), infix: undefined, precedence: Precedence.PrecNone };
			case TokenType.Identifier:
				return { prefix: () => this.identifierExpr(this.previous), infix: undefined, precedence: Precedence.PrecNone };
			case TokenType.String:
			case TokenType.StringEscape:
				return {
					prefix: () => {
						const t = this.previous;
						return { kind: 'Literal', start: t.start, end: t.end, token: t, value: t.value ?? '' };
					},
					infix: undefined,
					precedence: Precedence.PrecNone,
				};
			case TokenType.Number:
			case TokenType.NumberBin:
			case TokenType.NumberHex:
			case TokenType.True:
			case TokenType.False:
			case TokenType.Nil:
				return {
					prefix: () => {
						const t = this.previous;
						return { kind: 'Literal', start: t.start, end: t.end, token: t };
					},
					infix: undefined,
					precedence: Precedence.PrecNone,
				};
			case TokenType.ModuleMath:
			case TokenType.ModuleArray:
			case TokenType.ModuleObject:
			case TokenType.ModuleString:
			case TokenType.ModuleTime:
			case TokenType.ModuleCtor:
			case TokenType.ModuleSys:
				return {
					prefix: () => {
						const t = this.previous;
						return { kind: 'Module', start: t.start, end: t.end, token: t };
					},
					infix: undefined,
					precedence: Precedence.PrecNone,
				};
			case TokenType.Lambda:
				return { prefix: () => this.lambda(), infix: undefined, precedence: Precedence.PrecNone };
			case TokenType.Import:
				return {
					prefix: () => {
						const importToken = this.previous;
						const path = this.expression();
						return { kind: 'Import', start: importToken.start, end: path.end, importToken, path };
					},
					infix: undefined,
					precedence: Precedence.PrecNone,
				};
			case TokenType.This:
				return {
					prefix: () => {
						const t = this.previous;
						return { kind: 'This', start: t.start, end: t.end, token: t };
					},
					infix: undefined,
					precedence: Precedence.PrecNone,
				};
			case TokenType.Super:
				return {
					prefix: () => {
						const token = this.previous;
						this.consume(TokenType.Dot, "Expect '.' after 'super'.");
						const method = this.consume(TokenType.Identifier, 'Expect superclass method name.');
						return { kind: 'Super', start: token.start, end: method.end, token, method };
					},
					infix: undefined,
					precedence: Precedence.PrecNone,
				};
			case TokenType.LBrace:
				return { prefix: () => this.objectLiteral(), infix: undefined, precedence: Precedence.PrecNone };
			default:
				return { prefix: undefined, infix: undefined, precedence: Precedence.PrecNone };
		}
	}

	private parsePrecedence(precedence: Precedence): Expr {
		this.advance();
		const canAssign = precedence <= Precedence.PrecAssignment;

		const rule = this.getRule(this.previous.type);
		if (!rule.prefix) {
			this.errorAtCurrent('Expect expression.');
			const t = this.previous;
			return { kind: 'Literal', start: t.start, end: t.end, token: t };
		}

		let expr = rule.prefix();

		while (precedence <= this.getRule(this.peek.type).precedence) {
			this.advance();
			const infix = this.getRule(this.previous.type).infix;
			if (infix) {
				expr = infix(expr, canAssign);
			}
		}

		if (canAssign && this.match(TokenType.Equal)) {
			const op = this.previous;
			if (!isValidAssignmentTarget(expr)) {
				this.error('Invalid assignment target.', op);
			}
			const value = this.parsePrecedence(Precedence.PrecAssignment);
			expr = { kind: 'Assignment', start: expr.start, end: value.end, op, target: expr, value };
		}

		return expr;
	}

	private grouping(): Expr {
		const inner = this.expression();
		this.consume(TokenType.RParen, "Expect ')' after expression.");
		return inner;
	}

	private unary(): Expr {
		const op = this.previous;
		const operand = this.parsePrecedence(Precedence.PrecUnary);
		return { kind: 'Unary', start: op.start, end: operand.end, op, operand };
	}

	private binary(left: Expr): Expr {
		const op = this.previous;
		const rule = this.getRule(op.type);
		const right = this.parsePrecedence((rule.precedence + 1) as Precedence);
		return { kind: 'Binary', start: left.start, end: right.end, op, left, right };
	}

	private logical(left: Expr, op: 'and' | 'or'): Expr {
		const opToken = this.previous;
		const right = this.parsePrecedence(op === 'and' ? Precedence.PrecAnd : Precedence.PrecOr);
		return { kind: 'Logical', start: left.start, end: right.end, op: opToken, left, right };
	}

	private identifierExpr(token: Token): Expr {
		return { kind: 'Identifier', start: token.start, end: token.end, token };
	}

	private call(callee: Expr): Expr {
		const lparen = this.previous;
		const args: Expr[] = [];
		if (!this.check(TokenType.RParen)) {
			do {
				if (args.length >= 255) {
					this.errorAtCurrent("Can't have more than 255 arguments.");
				}
				args.push(this.expression());
			} while (this.match(TokenType.Comma));
		}
		this.consume(TokenType.RParen, "Expect ')' after arguments.");
		const rparen = this.previous.end;
		return { kind: 'Call', start: callee.start, end: rparen, callee, lparen, rparen, args };
	}

	private dot(object: Expr, _canAssign: boolean): Expr {
		const dot = this.previous;
		const name = this.consume(TokenType.Identifier, "Expect property name after '.'.");

		if (this.match(TokenType.LParen)) {
			// invoke: obj.method(args)
			const lparen = this.previous;
			const args: Expr[] = [];
			if (!this.check(TokenType.RParen)) {
				do {
					if (args.length >= 255) {
						this.errorAtCurrent("Can't have more than 255 arguments.");
					}
					args.push(this.expression());
				} while (this.match(TokenType.Comma));
			}
			this.consume(TokenType.RParen, "Expect ')' after arguments.");
			const rparen = this.previous.end;
			const prop: Expr = { kind: 'Property', start: object.start, end: name.end, object, dot, name };
			return { kind: 'Call', start: object.start, end: rparen, callee: prop, lparen, rparen, args };
		}

		return { kind: 'Property', start: object.start, end: name.end, object, dot, name };
	}

	private subscript(object: Expr, _canAssign: boolean): Expr {
		const lbracket = this.previous;
		const index = this.expression();
		this.consume(TokenType.RBracket, "Expect ']' after subscript.");
		const rbracket = this.previous.end;
		return { kind: 'Subscript', start: object.start, end: rbracket, object, lbracket, rbracket, index };
	}

	private arrayLiteral(): Expr {
		const lbracket = this.previous;
		const elements: Expr[] = [];
		if (!this.check(TokenType.RBracket) && !this.check(TokenType.EOF)) {
			do {
				elements.push(this.expression());
				if (this.panicMode) {
					break;
				}
			} while (this.match(TokenType.Comma));
		}
		this.consume(TokenType.RBracket, "Expect ']' to close the array.");
		return { kind: 'ArrayLiteral', start: lbracket.start, end: this.previous.end, lbracket, elements };
	}

	private objectLiteral(): Expr {
		const lbrace = this.previous;
		const properties: ObjectProperty[] = [];
		if (!this.check(TokenType.RBrace) && !this.check(TokenType.EOF)) {
			do {
				let key: Token;
				if (this.match(TokenType.Identifier) || this.match(TokenType.String) || this.match(TokenType.StringEscape)) {
					key = this.previous;
				} else {
					this.errorAtCurrent('Expect property name.');
					break;
				}

				this.consume(TokenType.Colon, "Expect ':' after property name.");
				const value = this.expression();
				properties.push({ key, value });

				if (this.panicMode) {
					break;
				}
			} while (this.match(TokenType.Comma));
		}
		this.consume(TokenType.RBrace, "Expect '}' to close the object.");
		return { kind: 'ObjectLiteral', start: lbrace.start, end: this.previous.end, lbrace, properties };
	}

	private lambda(): Expr {
		const lambda = this.previous;
		this.consume(TokenType.LParen, "Expect '(' before lambda parameters.");

		const params: Token[] = [];
		if (!this.check(TokenType.RParen)) {
			do {
				if (params.length >= 255) {
					this.errorAtCurrent("Can't have more than 255 parameters.");
				}
				params.push(this.consume(TokenType.Identifier, 'Expect parameter name.'));
			} while (this.match(TokenType.Comma));
		}

		this.consume(TokenType.RParen, "Expect ')' after function parameters.");

		let body: BlockStmt | Expr;
		let isArrow = false;
		if (this.match(TokenType.RightArrow)) {
			isArrow = true;
			body = this.parsePrecedence(Precedence.PrecAssignment);
		} else {
			this.consume(TokenType.LBrace, "Expect '{' before lambda body.");
			body = this.block();
		}

		return {
			kind: 'Lambda',
			start: lambda.start,
			end: body.end,
			lambda,
			params,
			body,
			isArrow,
		};
	}
}

function isValidAssignmentTarget(expr: Expr): boolean {
	if (expr.kind === 'Identifier' || expr.kind === 'Subscript' || expr.kind === 'Property') {
		return true;
	}
	return false;
}

/** Parse a token stream into a Script AST. */
export function parse(tokens: Token[]): Script {
	return new Parser(tokens).parse();
}
