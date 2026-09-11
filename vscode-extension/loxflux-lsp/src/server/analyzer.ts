/*
 * MIT License
 * Copyright (c) 2025 IMSDCrueoft
 * LoxFlux language server - symbol analysis (scopes, resolution, lints).
 */

import { Comment, Token, TokenType } from './lexer';
import { BlockStmt, Expr, Script, Stmt } from './ast';
import { Positions } from './positions';

const STMT_KINDS = new Set<string>([
	'Print', 'ExprStmt', 'VarDecl', 'FunDecl', 'ClassDecl', 'If', 'While', 'DoWhile',
	'For', 'Branch', 'Return', 'Throw', 'Export', 'Break', 'Continue', 'Block',
]);

export enum DeclKind {
	Local,
	Global,
	Param,
	Function,
	Class,
	Method,
}

export function declKindLabel(kind: DeclKind): string {
	switch (kind) {
		case DeclKind.Local: return 'variable';
		case DeclKind.Global: return 'global variable';
		case DeclKind.Param: return 'parameter';
		case DeclKind.Function: return 'function';
		case DeclKind.Class: return 'class';
		case DeclKind.Method: return 'method';
	}
}

export interface Symbol {
	name: string;
	kind: DeclKind;
	isConst: boolean;
	/** name token */
	nameToken: Token;
	/** full declaration node range */
	declStart: number;
	declEnd: number;
	/** doc comment (markdown) */
	doc: string | undefined;
	/** method container class name */
	container: string | undefined;
	/** parameter names for functions/methods/lambdas */
	params: Token[] | undefined;
	/** declared at script level (globals are visible anywhere in the document) */
	isGlobal: boolean;
	/** textual range of the scope the symbol lives in (for completion filtering) */
	scopeStart: number;
	scopeEnd: number;
	/** initializer expression for variables (used for type inference) */
	initializer: Expr | undefined;
	/** symbol is referenced somewhere */
	used: boolean;
	references: Token[];
}

export interface Usage {
	token: Token;
	isWrite: boolean;
	/** resolved declaration, if any */
	resolved: Symbol | undefined;
}

export interface ClassInfo {
	name: string;
	symbol: Symbol;
	superclassName: string | undefined;
	methods: Map<string, Symbol>;
	/** fields used through `this.x` inside methods */
	fields: Map<string, Token>;
	declStart: number;
	declEnd: number;
}

export interface ImportInfo {
	importToken: Token;
	/** string literal path */
	path: string | undefined;
	/** range of the path literal expression */
	pathStart: number;
	pathEnd: number;
}

export interface ExportedName {
	name: string;
	/** symbol exported by reference (e.g. `export obj;`) */
	symbol: Symbol | undefined;
	/** range to navigate to inside this file */
	targetStart: number;
	targetEnd: number;
	hint: 'function' | 'class' | 'variable';
	doc: string | undefined;
}

export interface Diagnostic {
	message: string;
	start: number;
	end: number;
	severity: 'error' | 'warning' | 'info' | 'hint';
	code?: string;
}

export interface LintSettings {
	undeclaredVariable: 'error' | 'warning' | 'off';
	unusedVariable: boolean;
	assignToConst: boolean;
	unreachableCode: boolean;
}

export const DEFAULT_LINT_SETTINGS: LintSettings = {
	undeclaredVariable: 'warning',
	unusedVariable: false,
	assignToConst: true,
	unreachableCode: true,
};

export interface Analysis {
	script: Script;
	symbols: Symbol[];
	globals: Symbol[];
	usages: Usage[];
	classes: Map<string, ClassInfo>;
	imports: ImportInfo[];
	exports: ExportedName[];
	/** all property names used in the document (for member completion heuristics) */
	propertyNames: Map<string, Token>;
	diagnostics: Diagnostic[];
	comments: Comment[];
}

interface Scope {
	symbols: Map<string, Symbol[]>;
	/** script-level scope? */
	isScript: boolean;
	/** textual range of the scope (block/function body/script) */
	start: number;
	end: number;
}

export class Analyzer {
	private readonly script: Script;
	private readonly lint: LintSettings;
	private readonly positions: Positions;
	private readonly sourceLength: number;

	private readonly symbols: Symbol[] = [];
	private readonly usages: Usage[] = [];
	private readonly classes = new Map<string, ClassInfo>();
	private readonly imports: ImportInfo[] = [];
	private readonly exports: ExportedName[] = [];
	private readonly diagnostics: Diagnostic[] = [];
	private readonly propertyNames = new Map<string, Token>();

	private readonly scopes: Scope[] = [];
	private currentClass: ClassInfo | undefined;
	/** nesting depth of loops (for break/continue checks) */
	private loopDepth = 0;
	/** nesting depth of function/lambda bodies (for return checks) */
	private functionDepth = 0;

	constructor(script: Script, source: string, lint: LintSettings) {
		this.script = script;
		this.lint = lint;
		this.positions = new Positions(source);
		this.sourceLength = source.length;
	}

	analyze(): Analysis {
		this.collectSyntaxDiagnostics();

		this.scopes.push({ symbols: new Map(), isScript: true, start: 0, end: this.sourceLength });
		for (const stmt of this.script.statements) {
			this.walkStmt(stmt);
		}
		this.scopes.pop();

		if (this.lint.unusedVariable) {
			this.reportUnusedLocals();
		}
		if (this.lint.unreachableCode) {
			this.reportUnreachable(this.script.statements);
		}

		return {
			script: this.script,
			symbols: this.symbols,
			globals: this.symbols.filter((s) => s.isGlobal),
			usages: this.usages,
			classes: this.classes,
			imports: this.imports,
			exports: this.exports,
			propertyNames: this.propertyNames,
			diagnostics: this.diagnostics,
			comments: this.script.comments,
		};
	}

	// ---- setup ---------------------------------------------------------

	private collectSyntaxDiagnostics(): void {
		for (const err of this.script.errors) {
			this.diagnostics.push({ message: err.message, start: err.start, end: err.end, severity: 'error', code: 'syntax' });
		}
		// lexer error tokens the parser skipped silently
		const reported = new Set(this.script.errors.map((e) => `${e.start}:${e.end}`));
		for (const t of this.script.tokens) {
			if (t.type === TokenType.Error && !reported.has(`${t.start}:${t.end}`)) {
				this.diagnostics.push({
					message: t.errorMessage ?? 'Unexpected character.',
					start: t.start,
					end: t.end > t.start ? t.end : t.start + 1,
					severity: 'error',
					code: 'syntax',
				});
			}
		}
	}

	// ---- scopes --------------------------------------------------------

	private pushScope(isScript = false, start = 0, end = Number.MAX_SAFE_INTEGER): void {
		this.scopes.push({ symbols: new Map(), isScript, start, end });
	}

	private popScope(): void {
		this.scopes.pop();
	}

	private declare(
		nameToken: Token,
		kind: DeclKind,
		opts: {
			isConst?: boolean;
			declStart?: number;
			declEnd?: number;
			doc?: string;
			container?: string;
			params?: Token[];
			initializer?: Expr;
			/** methods share the class scope; the real compiler does not
			 *  run the duplicate check for them */
			allowDuplicate?: boolean;
		} = {},
	): Symbol {
		const scope = this.scopes[this.scopes.length - 1];

		// mirror the real compiler: locals may not be redeclared in the same
		// scope (globals and class methods are exempt)
		if (!scope.isScript && !opts.allowDuplicate) {
			const existing = scope.symbols.get(nameToken.text);
			if (existing && existing.length > 0) {
				this.diagnostics.push({
					message: 'Already a variable with this name in this scope.',
					start: nameToken.start,
					end: nameToken.end,
					severity: 'error',
					code: 'duplicate-local',
				});
			}
		}

		const symbol: Symbol = {
			name: nameToken.text,
			kind,
			isConst: opts.isConst ?? false,
			nameToken,
			declStart: opts.declStart ?? nameToken.start,
			declEnd: opts.declEnd ?? nameToken.end,
			doc: opts.doc,
			container: opts.container,
			params: opts.params,
			isGlobal: this.scopes[this.scopes.length - 1].isScript,
			scopeStart: this.scopes[this.scopes.length - 1].start,
			scopeEnd: this.scopes[this.scopes.length - 1].end,
			initializer: opts.initializer,
			used: false,
			references: [],
		};
		this.symbols.push(symbol);

		const list = scope.symbols.get(symbol.name);
		if (list) {
			list.push(symbol);
		} else {
			scope.symbols.set(symbol.name, [symbol]);
		}
		return symbol;
	}

	private resolve(token: Token): Symbol | undefined {
		// innermost scope outwards; locals must be declared before use
		for (let i = this.scopes.length - 1; i >= 0; i--) {
			const scope = this.scopes[i];
			const list = scope.symbols.get(token.text);
			if (list) {
				let best: Symbol | undefined;
				for (const sym of list) {
					if (sym.nameToken.start <= token.start) {
						if (!best || sym.nameToken.start > best.nameToken.start) {
							best = sym;
						}
					}
				}
				if (best) {
					return best;
				}
			}
		}

		// globals are hoisted: visible regardless of declaration order
		for (const scope of this.scopes) {
			if (!scope.isScript) {
				continue;
			}
			const list = scope.symbols.get(token.text);
			if (list && list.length > 0) {
				return list[list.length - 1];
			}
		}
		return undefined;
	}

	private addUsage(token: Token, isWrite: boolean): void {
		const resolved = this.resolve(token);
		if (resolved) {
			resolved.used = true;
			resolved.references.push(token);
		}
		this.usages.push({ token, isWrite, resolved });

		if (isWrite && resolved && resolved.isConst && this.lint.assignToConst) {
			this.diagnostics.push({
				message: `Cannot assign to constant '${resolved.name}'.`,
				start: token.start,
				end: token.end,
				severity: 'error',
				code: 'assign-to-const',
			});
		}

		if (!resolved && this.lint.undeclaredVariable !== 'off') {
			this.diagnostics.push({
				message: `Undefined variable '${token.text}'.`,
				start: token.start,
				end: token.end,
				severity: this.lint.undeclaredVariable,
				code: 'undeclared',
			});
		}
	}

	// ---- doc comments --------------------------------------------------

	/** Extract the doc comment attached to a declaration at `offset`. */
	private docFor(offset: number): string | undefined {
		const comments = this.script.comments;
		if (comments.length === 0) {
			return undefined;
		}

		const declLine = this.positions.lineOf(offset);

		// block comment ending right above (or just before on the same line)
		for (const c of comments) {
			if (!c.isLine) {
				const endLine = this.positions.lineOf(c.end - 1);
				if (endLine === declLine - 1 || (endLine === declLine && c.end <= offset)) {
					return formatBlockComment(c);
				}
			}
		}

		// chain of `//` line comments directly above
		const byLine = new Map<number, Comment[]>();
		for (const c of comments) {
			if (!c.isLine) {
				continue;
			}
			const line = this.positions.lineOf(c.start);
			const list = byLine.get(line);
			if (list) {
				list.push(c);
			} else {
				byLine.set(line, [c]);
			}
		}

		const lines: string[] = [];
		let line = declLine - 1;
		while (line >= 0) {
			const c = byLine.get(line);
			if (!c || c.length === 0) {
				break;
			}
			// take the last comment on that line
			lines.unshift(cleanLineComment(c[c.length - 1].text));
			line--;
		}
		return lines.length > 0 ? lines.join('\n') : undefined;
	}

	// ---- statement walking ---------------------------------------------

	private walkStmt(stmt: Stmt): void {
		switch (stmt.kind) {
			case 'VarDecl': {
				// mirror the real compiler: const is only allowed in local scope
				if (stmt.isConst && this.scopes[this.scopes.length - 1].isScript) {
					this.diagnostics.push({
						message: 'Constant can only be defined in the local scope.',
						start: stmt.declToken.start,
						end: stmt.declToken.end,
						severity: 'error',
						code: 'const-scope',
					});
				}
				for (const d of stmt.declarators) {
					if (d.initializer) {
						this.walkExpr(d.initializer);
					}
				}
				// declare after initializers so `var a = a;` reads the outer `a`
				for (const d of stmt.declarators) {
					this.declare(d.name, this.scopes[this.scopes.length - 1].isScript ? DeclKind.Global : DeclKind.Local, {
						isConst: stmt.isConst,
						declStart: stmt.start,
						declEnd: stmt.end,
						doc: this.docFor(d.name.start),
						initializer: d.initializer,
					});
				}
				break;
			}
			case 'FunDecl': {
				const symbol = this.declare(stmt.name, DeclKind.Function, {
					declStart: stmt.start,
					declEnd: stmt.end,
					doc: this.docFor(stmt.name.start),
					params: stmt.params,
				});
				this.walkFunction(stmt.params, stmt.body);
				symbol.declEnd = stmt.body.end;
				break;
			}
			case 'ClassDecl': {
				const symbol = this.declare(stmt.name, DeclKind.Class, {
					declStart: stmt.start,
					declEnd: stmt.end,
					doc: this.docFor(stmt.name.start),
				});
				if (stmt.superclass) {
					this.walkExpr(stmt.superclass);
				}
				const info: ClassInfo = {
					name: stmt.name.text,
					symbol,
					superclassName: stmt.superclass?.token.text,
					methods: new Map(),
					fields: new Map(),
					declStart: stmt.start,
					declEnd: stmt.end,
				};
				this.classes.set(stmt.name.text, info);

				const prevClass = this.currentClass;
				this.currentClass = info;
				this.pushScope(false, stmt.start, stmt.end);
				try {
					for (const method of stmt.methods) {
						const mSymbol = this.declare(method.name, DeclKind.Method, {
							declStart: method.start,
							declEnd: method.body.end,
							doc: this.docFor(method.name.start),
							container: stmt.name.text,
							params: method.params,
							allowDuplicate: true,
						});
						info.methods.set(method.name.text, mSymbol);
						this.walkFunction(method.params, method.body);
					}
				} finally {
					this.popScope();
					this.currentClass = prevClass;
				}
				break;
			}
			case 'Block': {
				this.pushScope(false, stmt.start, stmt.end);
				try {
					for (const s of stmt.statements) {
						this.walkStmt(s);
					}
				} finally {
					this.popScope();
				}
				break;
			}
			case 'If':
				this.walkExpr(stmt.condition);
				this.walkStmt(stmt.thenBranch);
				if (stmt.elseBranch) {
					this.walkStmt(stmt.elseBranch);
				}
				break;
			case 'While':
				this.walkExpr(stmt.condition);
				this.loopDepth++;
				try {
					this.walkStmt(stmt.body);
				} finally {
					this.loopDepth--;
				}
				break;
			case 'DoWhile':
				this.loopDepth++;
				try {
					this.walkStmt(stmt.body);
				} finally {
					this.loopDepth--;
				}
				this.walkExpr(stmt.condition);
				break;
			case 'For': {
				this.pushScope(false, stmt.start, stmt.end);
				try {
					if (stmt.initializer) {
						this.walkStmt(stmt.initializer);
					}
					if (stmt.condition) {
						this.walkExpr(stmt.condition);
					}
					if (stmt.increment) {
						this.walkExpr(stmt.increment);
					}
					this.loopDepth++;
					try {
						this.walkStmt(stmt.body);
					} finally {
						this.loopDepth--;
					}
				} finally {
					this.popScope();
				}
				break;
			}
			case 'Branch':
				for (const c of stmt.cases) {
					if (c.condition) {
						this.walkExpr(c.condition);
					}
					this.walkStmt(c.body);
				}
				break;
			case 'Print':
				this.walkExpr(stmt.expression);
				break;
			case 'ExprStmt':
				this.walkExpr(stmt.expression);
				break;
			case 'Return':
				if (this.functionDepth === 0) {
					this.diagnostics.push({
						message: "Can't return from top-level code.",
						start: stmt.returnToken.start,
						end: stmt.returnToken.end,
						severity: 'error',
						code: 'return-outside',
					});
				}
				if (stmt.value) {
					this.walkExpr(stmt.value);
				}
				break;
			case 'Throw':
				this.walkExpr(stmt.value);
				break;
			case 'Export':
				if (stmt.value) {
					this.collectExport(stmt.value);
					this.walkExpr(stmt.value);
				}
				break;
			case 'Break':
				if (this.loopDepth === 0) {
					this.diagnostics.push({
						message: "Cannot use 'break' outside of a loop.",
						start: stmt.token.start,
						end: stmt.token.end,
						severity: 'error',
						code: 'break-outside',
					});
				}
				break;
			case 'Continue':
				if (this.loopDepth === 0) {
					this.diagnostics.push({
						message: "Cannot use 'continue' outside of a loop.",
						start: stmt.token.start,
						end: stmt.token.end,
						severity: 'error',
						code: 'continue-outside',
					});
				}
				break;
		}
	}

	private walkFunction(params: Token[], body: Stmt | Expr): void {
		this.pushScope(false, body.start, body.end);
		this.functionDepth++;
		try {
			for (const p of params) {
				this.declare(p, DeclKind.Param, {});
			}
			if (body.kind === 'Block') {
				for (const s of (body as BlockStmt).statements) {
					this.walkStmt(s);
				}
			} else if (STMT_KINDS.has(body.kind)) {
				this.walkStmt(body as Stmt);
			} else {
				this.walkExpr(body as Expr);
			}
		} finally {
			this.functionDepth--;
			this.popScope();
		}
	}

	// ---- expression walking ---------------------------------------------

	private walkExpr(expr: Expr): void {
		switch (expr.kind) {
			case 'Identifier':
				this.addUsage(expr.token, false);
				break;
			case 'Literal':
			case 'Module':
				break;
			case 'This':
				if (!this.currentClass) {
					this.diagnostics.push({
						message: "Can't use 'this' outside of a class.",
						start: expr.token.start,
						end: expr.token.end,
						severity: 'error',
						code: 'this-outside',
					});
				}
				break;
			case 'Super':
				if (!this.currentClass) {
					this.diagnostics.push({
						message: "Can't use 'super' outside of a class.",
						start: expr.token.start,
						end: expr.method.end,
						severity: 'error',
						code: 'super-outside',
					});
				} else if (!this.currentClass.superclassName) {
					this.diagnostics.push({
						message: "Can't use 'super' in a class with no superclass.",
						start: expr.token.start,
						end: expr.method.end,
						severity: 'error',
						code: 'super-no-superclass',
					});
				} else {
					this.currentClass.symbol.used = true;
				}
				break;
			case 'Unary':
				this.walkExpr(expr.operand);
				break;
			case 'Binary':
				this.walkExpr(expr.left);
				this.walkExpr(expr.right);
				break;
			case 'Logical':
				this.walkExpr(expr.left);
				this.walkExpr(expr.right);
				break;
			case 'Assignment':
				if (expr.target.kind === 'Identifier') {
					this.addUsage(expr.target.token, true);
				} else {
					this.walkExpr(expr.target);
				}
				this.walkExpr(expr.value);
				break;
			case 'Call':
				this.walkExpr(expr.callee);
				for (const a of expr.args) {
					this.walkExpr(a);
				}
				break;
			case 'Property':
				this.propertyNames.set(expr.name.text, expr.name);
				if (expr.object.kind === 'This' && this.currentClass) {
					this.currentClass.fields.set(expr.name.text, expr.name);
				}
				this.walkExpr(expr.object);
				break;
			case 'Subscript':
				if (expr.index.kind === 'Literal' && expr.index.token.type === TokenType.String) {
					this.propertyNames.set(expr.index.value ?? '', expr.index.token);
				}
				this.walkExpr(expr.object);
				this.walkExpr(expr.index);
				break;
			case 'Lambda':
				this.walkFunction(expr.params, expr.body);
				break;
			case 'ArrayLiteral':
				for (const e of expr.elements) {
					this.walkExpr(e);
				}
				break;
			case 'ObjectLiteral':
				for (const p of expr.properties) {
					this.walkExpr(p.value);
				}
				break;
			case 'Import': {
				const info: ImportInfo = {
					importToken: expr.importToken,
					path: expr.path.kind === 'Literal' && expr.path.value !== undefined ? expr.path.value : undefined,
					pathStart: expr.path.start,
					pathEnd: expr.path.end,
				};
				this.imports.push(info);
				if (expr.path.kind !== 'Literal') {
					this.walkExpr(expr.path);
				}
				break;
			}
		}
	}

	// ---- exports -------------------------------------------------------

	private collectExport(value: Expr): void {
		if (value.kind === 'Identifier') {
			const resolved = this.resolve(value.token);
			this.exports.push({
				name: value.token.text,
				symbol: resolved,
				targetStart: resolved ? resolved.nameToken.start : value.start,
				targetEnd: resolved ? resolved.nameToken.end : value.end,
				hint: this.hintOf(resolved),
				doc: resolved?.doc,
			});
			return;
		}
		if (value.kind === 'ObjectLiteral') {
			for (const p of value.properties) {
				const isStringKey = p.key.type === TokenType.String || p.key.type === TokenType.StringEscape;
				const key = isStringKey ? (p.key.value ?? p.key.text) : p.key.text;
				let symbol: Symbol | undefined;
				let targetStart = p.key.start;
				let targetEnd = p.key.end;
				let hint: 'function' | 'class' | 'variable' = 'variable';

				if (p.value.kind === 'Identifier') {
					symbol = this.resolve(p.value.token);
					targetStart = symbol ? symbol.nameToken.start : p.value.start;
					targetEnd = symbol ? symbol.nameToken.end : p.value.end;
					hint = this.hintOf(symbol);
				} else if (p.value.kind === 'Lambda') {
					hint = 'function';
				}

				this.exports.push({
					name: key,
					symbol,
					targetStart,
					targetEnd,
					hint,
					doc: symbol?.doc ?? this.docFor(p.key.start),
				});
			}
			return;
		}
		// any other expression: export the value itself
		this.exports.push({
			name: value.kind === 'Lambda' ? '<lambda>' : '<expr>',
			symbol: undefined,
			targetStart: value.start,
			targetEnd: value.end,
			hint: value.kind === 'Lambda' ? 'function' : 'variable',
			doc: undefined,
		});
	}

	private hintOf(symbol: Symbol | undefined): 'function' | 'class' | 'variable' {
		if (!symbol) {
			return 'variable';
		}
		if (symbol.kind === DeclKind.Function || symbol.kind === DeclKind.Method) {
			return 'function';
		}
		if (symbol.kind === DeclKind.Class) {
			return 'class';
		}
		return 'variable';
	}

	// ---- lints ---------------------------------------------------------

	private reportUnusedLocals(): void {
		for (const sym of this.symbols) {
			if (sym.kind === DeclKind.Local && !sym.used && sym.name !== '_') {
				this.diagnostics.push({
					message: `Unused local variable '${sym.name}'.`,
					start: sym.nameToken.start,
					end: sym.nameToken.end,
					severity: 'warning',
					code: 'unused',
				});
			}
		}
	}

	private reportUnreachable(statements: Stmt[]): void {
		let terminated = false;
		for (const stmt of statements) {
			if (terminated) {
				this.diagnostics.push({
					message: 'Unreachable code.',
					start: stmt.start,
					end: stmt.start + 1,
					severity: 'warning',
					code: 'unreachable',
				});
			}
			if (stmt.kind === 'Return' || stmt.kind === 'Throw' || stmt.kind === 'Break' || stmt.kind === 'Continue') {
				terminated = true;
			}
			switch (stmt.kind) {
				case 'Block':
					this.reportUnreachable(stmt.statements);
					break;
				case 'If':
					this.reportUnreachable([stmt.thenBranch]);
					if (stmt.elseBranch) {
						this.reportUnreachable([stmt.elseBranch]);
					}
					break;
				case 'While':
					this.reportUnreachable([stmt.body]);
					break;
				case 'DoWhile':
					this.reportUnreachable([stmt.body]);
					break;
				case 'For':
					this.reportUnreachable([stmt.body]);
					break;
				case 'Branch':
					for (const c of stmt.cases) {
						this.reportUnreachable([c.body]);
					}
					break;
				case 'FunDecl':
					this.reportUnreachable(stmt.body.statements);
					break;
				case 'ClassDecl':
					for (const m of stmt.methods) {
						this.reportUnreachable(m.body.statements);
					}
					break;
				default:
					break;
			}
		}
	}
}

function cleanLineComment(text: string): string {
	return text.replace(/^\s*\/\/\s?/, '').trimEnd();
}

function formatBlockComment(c: Comment): string {
	let t = c.text;
	if (t.startsWith('/*')) {
		t = t.slice(2);
	}
	if (t.endsWith('*/')) {
		t = t.slice(0, -2);
	}
	return t
		.split('\n')
		.map((l) => l.replace(/^\s*\*\s?/, '').trimEnd())
		.join('\n')
		.trim();
}
