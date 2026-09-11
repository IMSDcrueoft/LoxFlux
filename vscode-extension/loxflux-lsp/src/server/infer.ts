/*
 * MIT License
 * Copyright (c) 2025 IMSDCrueoft
 * LoxFlux language server - lightweight receiver type inference.
 */

import { Expr } from './ast';
import { ClassInfo, DeclKind, Symbol } from './analyzer';
import { ParsedDocument } from './documents';
import { DocumentStore } from './documents';
import { walkScript } from './walker';

export type ReceiverInfo =
	| { kind: 'module'; moduleKey: string }
	| { kind: 'class'; info: ClassInfo; uri: string }
	| { kind: 'imports'; uri: string }
	| { kind: 'unknown' };

function usageSymbolAt(doc: ParsedDocument, tokenStart: number): Symbol | undefined {
	return doc.analysis.usages.find((u) => u.token.start === tokenStart)?.resolved;
}

function classOfSymbol(doc: ParsedDocument, uri: string, symbol: Symbol | undefined): ReceiverInfo {
	if (symbol && symbol.kind === DeclKind.Class) {
		const info = doc.analysis.classes.get(symbol.name);
		if (info) {
			return { kind: 'class', info, uri };
		}
	}
	return { kind: 'unknown' };
}

/** Infer what a receiver expression evaluates to (for `.` completions etc). */
export function inferReceiver(doc: ParsedDocument, expr: Expr | undefined, store: DocumentStore, depth = 0): ReceiverInfo {
	if (!expr || depth > 6) {
		return { kind: 'unknown' };
	}

	switch (expr.kind) {
		case 'Module':
			return { kind: 'module', moduleKey: expr.token.text };

		case 'This': {
			const className = findEnclosingClassOf(doc, expr.token.start);
			if (className) {
				const info = doc.analysis.classes.get(className);
				if (info) {
					return { kind: 'class', info, uri: doc.uri };
				}
			}
			return { kind: 'unknown' };
		}

		case 'Identifier': {
			const symbol = usageSymbolAt(doc, expr.token.start);
			if (symbol) {
				const fromDecl = classOfSymbol(doc, doc.uri, symbol);
				if (fromDecl.kind !== 'unknown') {
					return fromDecl;
				}
				return inferFromInitializer(doc, symbol, store, depth);
			}
			// maybe a class referenced before its usage was recorded
			const cls = doc.analysis.classes.get(expr.token.text);
			if (cls) {
				return { kind: 'class', info: cls, uri: doc.uri };
			}
			return { kind: 'unknown' };
		}

		case 'Call':
			return inferConstructor(doc, expr.callee, store, depth + 1);

		case 'Import': {
			if (expr.path.kind === 'Literal' && expr.path.value !== undefined) {
				const uri = store.resolveImport(doc, expr.path.value);
				if (uri) {
					return { kind: 'imports', uri };
				}
			}
			return { kind: 'unknown' };
		}

		default:
			return { kind: 'unknown' };
	}
}

function inferConstructor(doc: ParsedDocument, callee: Expr, store: DocumentStore, depth: number): ReceiverInfo {
	if (callee.kind === 'Module') {
		return { kind: 'module', moduleKey: callee.token.text };
	}
	if (callee.kind === 'Identifier') {
		const symbol = usageSymbolAt(doc, callee.token.start);
		if (symbol) {
			return classOfSymbol(doc, doc.uri, symbol);
		}
		const cls = doc.analysis.classes.get(callee.token.text);
		if (cls) {
			return { kind: 'class', info: cls, uri: doc.uri };
		}
		return { kind: 'unknown' };
	}
	if (callee.kind === 'Property') {
		// @ctor.Array() style: receiver must be a module
		const receiver = inferReceiver(doc, callee.object, store, depth + 1);
		if (receiver.kind === 'module') {
			return { kind: 'module', moduleKey: receiver.moduleKey };
		}
	}
	return { kind: 'unknown' };
}

/** Propagate types through a variable's initializer. */
function inferFromInitializer(doc: ParsedDocument, symbol: Symbol, store: DocumentStore, depth: number): ReceiverInfo {
	const init = symbol.initializer;
	if (!init) {
		return { kind: 'unknown' };
	}
	if (init.kind === 'Import') {
		return inferReceiver(doc, init, store, depth + 1);
	}
	if (init.kind === 'Call') {
		return inferConstructor(doc, init.callee, store, depth + 1);
	}
	if (init.kind === 'Identifier') {
		const other = usageSymbolAt(doc, init.token.start);
		if (other && other !== symbol) {
			return inferFromInitializer(doc, other, store, depth + 1);
		}
		return inferReceiver(doc, init, store, depth + 1);
	}
	if (init.kind === 'Property') {
		// module member binding: var arr = @array; (rare) or libSomething.obj
		return inferReceiver(doc, init.object, store, depth + 1);
	}
	return { kind: 'unknown' };
}

/** Find the innermost class declaration containing an offset. */
function findEnclosingClassOf(doc: ParsedDocument, offset: number): string | undefined {
	let best: string | undefined;
	let bestSize = Infinity;
	walkScript(doc.analysis.script, {
		stmt: (stmt) => {
			if (stmt.kind === 'ClassDecl' && stmt.start <= offset && offset <= stmt.end) {
				const size = stmt.end - stmt.start;
				if (size < bestSize) {
					bestSize = size;
					best = stmt.name.text;
				}
			}
		},
	});
	return best;
}
