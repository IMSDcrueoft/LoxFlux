/*
 * MIT License
 * Copyright (c) 2025 IMSDCrueoft
 * LoxFlux language server - AST definitions.
 * Positions are stored as 0-based offsets into the source text.
 */

import { Token, TokenType } from './lexer';

export interface Node {
	/** start offset */
	start: number;
	/** end offset (exclusive) */
	end: number;
}

export type Expr =
	| LiteralExpr
	| IdentifierExpr
	| ModuleExpr
	| UnaryExpr
	| BinaryExpr
	| LogicalExpr
	| AssignmentExpr
	| CallExpr
	| PropertyExpr
	| SubscriptExpr
	| LambdaExpr
	| ArrayLiteralExpr
	| ObjectLiteralExpr
	| ImportExpr
	| ThisExpr
	| SuperExpr;

export interface LiteralExpr extends Node {
	kind: 'Literal';
	token: Token;
	/** decoded string value when token is a string */
	value?: string;
}

export interface IdentifierExpr extends Node {
	kind: 'Identifier';
	token: Token;
}

export interface ModuleExpr extends Node {
	kind: 'Module';
	token: Token;
}

export interface UnaryExpr extends Node {
	kind: 'Unary';
	op: Token;
	operand: Expr;
}

export interface BinaryExpr extends Node {
	kind: 'Binary';
	op: Token;
	left: Expr;
	right: Expr;
}

export interface LogicalExpr extends Node {
	kind: 'Logical';
	op: Token;
	left: Expr;
	right: Expr;
}

export interface AssignmentExpr extends Node {
	kind: 'Assignment';
	op: Token;
	target: Expr;
	value: Expr;
}

export interface CallExpr extends Node {
	kind: 'Call';
	callee: Expr;
	lparen: Token;
	rparen: number;
	args: Expr[];
}

export interface PropertyExpr extends Node {
	kind: 'Property';
	object: Expr;
	dot: Token;
	name: Token;
}

export interface SubscriptExpr extends Node {
	kind: 'Subscript';
	object: Expr;
	lbracket: Token;
	rbracket: number;
	index: Expr;
}

export interface LambdaExpr extends Node {
	kind: 'Lambda';
	lambda: Token;
	params: Token[];
	body: BlockStmt | Expr;
	/** true when body is `=> expr` */
	isArrow: boolean;
}

export interface ArrayLiteralExpr extends Node {
	kind: 'ArrayLiteral';
	lbracket: Token;
	elements: Expr[];
}

export interface ObjectProperty {
	key: Token;
	value: Expr;
}

export interface ObjectLiteralExpr extends Node {
	kind: 'ObjectLiteral';
	lbrace: Token;
	properties: ObjectProperty[];
}

export interface ImportExpr extends Node {
	kind: 'Import';
	importToken: Token;
	path: Expr;
}

export interface ThisExpr extends Node {
	kind: 'This';
	token: Token;
}

export interface SuperExpr extends Node {
	kind: 'Super';
	token: Token;
	method: Token;
}

export type Stmt =
	| PrintStmt
	| ExprStmt
	| VarDeclStmt
	| FunDeclStmt
	| ClassDeclStmt
	| IfStmt
	| WhileStmt
	| DoWhileStmt
	| ForStmt
	| BranchStmt
	| ReturnStmt
	| ThrowStmt
	| ExportStmt
	| BreakStmt
	| ContinueStmt
	| BlockStmt;

export interface PrintStmt extends Node {
	kind: 'Print';
	printToken: Token;
	expression: Expr;
}

export interface ExprStmt extends Node {
	kind: 'ExprStmt';
	expression: Expr;
}

export interface VarDeclStmt extends Node {
	kind: 'VarDecl';
	declToken: Token;
	/** 'var' | 'const' */
	isConst: boolean;
	declarators: VarDeclarator[];
}

export interface VarDeclarator {
	name: Token;
	initializer: Expr | undefined;
}

export interface FunDeclStmt extends Node {
	kind: 'FunDecl';
	funToken: Token;
	name: Token;
	params: Token[];
	body: BlockStmt;
}

export interface ClassDeclStmt extends Node {
	kind: 'ClassDecl';
	classToken: Token;
	name: Token;
	superclass: IdentifierExpr | undefined;
	methods: FunDeclStmt[];
}

export interface IfStmt extends Node {
	kind: 'If';
	ifToken: Token;
	condition: Expr;
	thenBranch: Stmt;
	elseBranch: Stmt | undefined;
}

export interface WhileStmt extends Node {
	kind: 'While';
	whileToken: Token;
	condition: Expr;
	body: Stmt;
}

export interface DoWhileStmt extends Node {
	kind: 'DoWhile';
	doToken: Token;
	body: Stmt;
	condition: Expr;
}

export interface ForStmt extends Node {
	kind: 'For';
	forToken: Token;
	initializer: VarDeclStmt | ExprStmt | undefined;
	condition: Expr | undefined;
	increment: Expr | undefined;
	body: Stmt;
}

export interface BranchCase {
	/** 'none' token when this is the default case */
	noneToken: Token | undefined;
	condition: Expr | undefined;
	colon: Token;
	body: Stmt;
}

export interface BranchStmt extends Node {
	kind: 'Branch';
	branchToken: Token;
	cases: BranchCase[];
}

export interface ReturnStmt extends Node {
	kind: 'Return';
	returnToken: Token;
	value: Expr | undefined;
}

export interface ThrowStmt extends Node {
	kind: 'Throw';
	throwToken: Token;
	value: Expr;
}

export interface ExportStmt extends Node {
	kind: 'Export';
	exportToken: Token;
	value: Expr | undefined;
}

export interface BreakStmt extends Node {
	kind: 'Break';
	token: Token;
}

export interface ContinueStmt extends Node {
	kind: 'Continue';
	token: Token;
}

export interface BlockStmt extends Node {
	kind: 'Block';
	lbrace: Token;
	statements: Stmt[];
}

export interface Script {
	kind: 'Script';
	start: number;
	end: number;
	statements: Stmt[];
	/** lexed tokens for lightweight consumers */
	tokens: Token[];
	/** comments attached to this script */
	comments: import('./lexer').Comment[];
	/** parse errors */
	errors: ParseError[];
}

export interface ParseError {
	message: string;
	start: number;
	end: number;
}

/** Token type -> readable operator text for hovers/diagnostics. */
export function tokenTypeLabel(type: TokenType): string {
	switch (type) {
		case TokenType.LParen: return '(';
		case TokenType.RParen: return ')';
		case TokenType.LBrace: return '{';
		case TokenType.RBrace: return '}';
		case TokenType.LBracket: return '[';
		case TokenType.RBracket: return ']';
		case TokenType.Comma: return ',';
		case TokenType.Dot: return '.';
		case TokenType.Semicolon: return ';';
		case TokenType.Colon: return ':';
		case TokenType.Equal: return '=';
		case TokenType.RightArrow: return '=>';
		default: return '?';
	}
}

export function nodeEnd(n: Node): number {
	return n.end;
}
