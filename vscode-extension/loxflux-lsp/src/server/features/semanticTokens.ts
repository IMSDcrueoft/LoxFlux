/*
 * MIT License
 * Copyright (c) 2025 IMSDCrueoft
 * LoxFlux language server - semantic tokens.
 */

import { SemanticTokens, SemanticTokensBuilder } from 'vscode-languageserver';
import { Analysis, DeclKind, Symbol } from '../analyzer';
import { Positions } from '../positions';
import { walkScript } from '../walker';

export const SEMANTIC_TOKEN_LEGEND = {
	tokenTypes: [
		'namespace',
		'class',
		'method',
		'property',
		'variable',
		'parameter',
		'function',
	],
	tokenModifiers: [
		'declaration',
		'readonly',
		'defaultLibrary',
	],
};

const T = {
	namespace: 0,
	class: 1,
	method: 2,
	property: 3,
	variable: 4,
	parameter: 5,
	function: 6,
};

const M = {
	declaration: 1 << 0,
	readonly: 1 << 1,
	defaultLibrary: 1 << 2,
};

interface RawToken {
	line: number;
	character: number;
	length: number;
	type: number;
	modifiers: number;
}

function symbolToken(sym: Symbol): { type: number; modifiers: number } {
	switch (sym.kind) {
		case DeclKind.Param:
			return { type: T.parameter, modifiers: 0 };
		case DeclKind.Function:
			return { type: T.function, modifiers: 0 };
		case DeclKind.Class:
			return { type: T.class, modifiers: 0 };
		case DeclKind.Method:
			return { type: T.method, modifiers: 0 };
		default:
			return { type: T.variable, modifiers: sym.isConst ? M.readonly : 0 };
	}
}

export function computeSemanticTokens(analysis: Analysis, positions: Positions): SemanticTokens {
	const builder = new SemanticTokensBuilder();
	const raw: RawToken[] = [];

	const push = (start: number, end: number, type: number, modifiers: number): void => {
		const startPos = positions.positionAt(start);
		const endPos = positions.positionAt(end);
		if (startPos.line !== endPos.line) {
			return;
		}
		raw.push({
			line: startPos.line,
			character: startPos.character,
			length: endPos.character - startPos.character,
			type,
			modifiers,
		});
	};

	// property names invoked as methods: obj.name(...)
	const methodNames = new Set<number>();
	walkScript(analysis.script, {
		expr: (expr) => {
			if (expr.kind === 'Call' && expr.callee.kind === 'Property') {
				methodNames.add(expr.callee.name.start);
			}
		},
	});

	// declarations
	for (const sym of analysis.symbols) {
		const { type, modifiers } = symbolToken(sym);
		push(sym.nameToken.start, sym.nameToken.end, type, modifiers | M.declaration);
	}

	// identifier usages
	for (const usage of analysis.usages) {
		if (usage.resolved) {
			const { type, modifiers } = symbolToken(usage.resolved);
			push(usage.token.start, usage.token.end, type, modifiers);
		}
	}

	// module references (@math, @sys, ...) and property accesses
	walkScript(analysis.script, {
		expr: (expr) => {
			if (expr.kind === 'Module') {
				push(expr.token.start, expr.token.end, T.namespace, M.defaultLibrary);
			} else if (expr.kind === 'Property') {
				const isMethod = methodNames.has(expr.name.start);
				push(expr.name.start, expr.name.end, isMethod ? T.method : T.property, 0);
			}
		},
	});

	raw.sort((a, b) => (a.line - b.line) || (a.character - b.character));

	for (const t of raw) {
		builder.push(t.line, t.character, t.length, t.type, t.modifiers);
	}

	return builder.build();
}
