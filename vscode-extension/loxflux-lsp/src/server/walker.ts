/*
 * MIT License
 * Copyright (c) 2025 IMSDCrueoft
 * LoxFlux language server - shared AST traversal helpers.
 */

import { Expr, Script, Stmt } from './ast';

export interface WalkContext {
	/** enclosing class name, if inside a class body */
	currentClass: string | undefined;
	/** true inside a class method body */
	inMethod: boolean;
}

export interface WalkHandlers {
	stmt?: (stmt: Stmt, ctx: WalkContext) => void;
	expr?: (expr: Expr, ctx: WalkContext) => void;
}

export function walkScript(script: Script, handlers: WalkHandlers): void {
	const ctx: WalkContext = { currentClass: undefined, inMethod: false };
	for (const stmt of script.statements) {
		walkStmt(stmt, ctx, handlers);
	}
}

function walkStmt(stmt: Stmt, ctx: WalkContext, handlers: WalkHandlers): void {
	handlers.stmt?.(stmt, ctx);

	switch (stmt.kind) {
		case 'VarDecl':
			for (const d of stmt.declarators) {
				if (d.initializer) {
					walkExpr(d.initializer, ctx, handlers);
				}
			}
			break;
		case 'FunDecl':
			walkFunctionBody(stmt.body, ctx, handlers);
			break;
		case 'ClassDecl': {
			if (stmt.superclass) {
				walkExpr(stmt.superclass, ctx, handlers);
			}
			const prevClass = ctx.currentClass;
			const prevInMethod = ctx.inMethod;
			ctx.currentClass = stmt.name.text;
			ctx.inMethod = true;
			for (const m of stmt.methods) {
				walkFunctionBody(m.body, ctx, handlers);
			}
			ctx.currentClass = prevClass;
			ctx.inMethod = prevInMethod;
			break;
		}
		case 'Block':
			for (const s of stmt.statements) {
				walkStmt(s, ctx, handlers);
			}
			break;
		case 'If':
			walkExpr(stmt.condition, ctx, handlers);
			walkStmt(stmt.thenBranch, ctx, handlers);
			if (stmt.elseBranch) {
				walkStmt(stmt.elseBranch, ctx, handlers);
			}
			break;
		case 'While':
			walkExpr(stmt.condition, ctx, handlers);
			walkStmt(stmt.body, ctx, handlers);
			break;
		case 'DoWhile':
			walkStmt(stmt.body, ctx, handlers);
			walkExpr(stmt.condition, ctx, handlers);
			break;
		case 'For':
			if (stmt.initializer) {
				walkStmt(stmt.initializer, ctx, handlers);
			}
			if (stmt.condition) {
				walkExpr(stmt.condition, ctx, handlers);
			}
			if (stmt.increment) {
				walkExpr(stmt.increment, ctx, handlers);
			}
			walkStmt(stmt.body, ctx, handlers);
			break;
		case 'Branch':
			for (const c of stmt.cases) {
				if (c.condition) {
					walkExpr(c.condition, ctx, handlers);
				}
				walkStmt(c.body, ctx, handlers);
			}
			break;
		case 'Print':
			walkExpr(stmt.expression, ctx, handlers);
			break;
		case 'ExprStmt':
			walkExpr(stmt.expression, ctx, handlers);
			break;
		case 'Return':
			if (stmt.value) {
				walkExpr(stmt.value, ctx, handlers);
			}
			break;
		case 'Throw':
			walkExpr(stmt.value, ctx, handlers);
			break;
		case 'Export':
			if (stmt.value) {
				walkExpr(stmt.value, ctx, handlers);
			}
			break;
		case 'Break':
		case 'Continue':
			break;
	}
}

function walkFunctionBody(body: Stmt, ctx: WalkContext, handlers: WalkHandlers): void {
	if (body.kind === 'Block') {
		for (const s of body.statements) {
			walkStmt(s, ctx, handlers);
		}
	} else {
		walkStmt(body, ctx, handlers);
	}
}

function walkExpr(expr: Expr, ctx: WalkContext, handlers: WalkHandlers): void {
	handlers.expr?.(expr, ctx);

	switch (expr.kind) {
		case 'Literal':
		case 'Identifier':
		case 'Module':
		case 'This':
			break;
		case 'Super':
			break;
		case 'Unary':
			walkExpr(expr.operand, ctx, handlers);
			break;
		case 'Binary':
			walkExpr(expr.left, ctx, handlers);
			walkExpr(expr.right, ctx, handlers);
			break;
		case 'Logical':
			walkExpr(expr.left, ctx, handlers);
			walkExpr(expr.right, ctx, handlers);
			break;
		case 'Assignment':
			walkExpr(expr.target, ctx, handlers);
			walkExpr(expr.value, ctx, handlers);
			break;
		case 'Call':
			walkExpr(expr.callee, ctx, handlers);
			for (const a of expr.args) {
				walkExpr(a, ctx, handlers);
			}
			break;
		case 'Property':
			walkExpr(expr.object, ctx, handlers);
			break;
		case 'Subscript':
			walkExpr(expr.object, ctx, handlers);
			walkExpr(expr.index, ctx, handlers);
			break;
		case 'Lambda':
			if (expr.body.kind === 'Block') {
				for (const s of expr.body.statements) {
					walkStmt(s, ctx, handlers);
				}
			} else {
				walkExpr(expr.body, ctx, handlers);
			}
			break;
		case 'ArrayLiteral':
			for (const e of expr.elements) {
				walkExpr(e, ctx, handlers);
			}
			break;
		case 'ObjectLiteral':
			for (const p of expr.properties) {
				walkExpr(p.value, ctx, handlers);
			}
			break;
		case 'Import':
			if (expr.path.kind !== 'Literal') {
				walkExpr(expr.path, ctx, handlers);
			}
			break;
	}
}

/** Find the innermost class declaration containing `offset`. */
export function findEnclosingClass(script: Script, offset: number): string | undefined {
	let found: string | undefined;
	let foundRange = Infinity;
	walkScript(script, {
		stmt: (stmt) => {
			if (stmt.kind === 'ClassDecl' && stmt.start <= offset && offset <= stmt.end) {
				const size = stmt.end - stmt.start;
				if (size < foundRange) {
					foundRange = size;
					found = stmt.name.text;
				}
			}
		},
	});
	return found;
}
