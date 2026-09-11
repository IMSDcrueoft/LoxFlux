/*
 * MIT License
 * Copyright (c) 2025 IMSDCrueoft
 * LoxFlux language server - document & workspace symbols.
 */

import { DocumentSymbol, SymbolInformation, SymbolKind, Range } from 'vscode-languageserver';
import { Analysis, DeclKind } from '../analyzer';
import { FunDeclStmt, Stmt } from '../ast';
import { Positions } from '../positions';

function kindFor(kind: DeclKind, isConst: boolean): SymbolKind {
	switch (kind) {
		case DeclKind.Class:
			return SymbolKind.Class;
		case DeclKind.Method:
			return SymbolKind.Method;
		case DeclKind.Function:
			return SymbolKind.Function;
		case DeclKind.Param:
			return SymbolKind.Variable;
		default:
			return isConst ? SymbolKind.Constant : SymbolKind.Variable;
	}
}

function nameRangeOf(positions: Positions, start: number, end: number): Range {
	return positions.range(start, end);
}

function documentSymbolsFromStatements(stmts: Stmt[], positions: Positions): DocumentSymbol[] {
	const result: DocumentSymbol[] = [];

	for (const stmt of stmts) {
		if (stmt.kind === 'VarDecl') {
			for (const d of stmt.declarators) {
				result.push({
					name: d.name.text,
					detail: stmt.isConst ? 'const' : 'var',
					kind: kindFor(DeclKind.Local, stmt.isConst),
					range: positions.range(stmt.start, stmt.end),
					selectionRange: nameRangeOf(positions, d.name.start, d.name.end),
				});
			}
		} else if (stmt.kind === 'FunDecl') {
			const params = stmt.params.map((p) => p.text).join(', ');
			result.push({
				name: `${stmt.name.text}(${params})`,
				detail: 'fun',
				kind: SymbolKind.Function,
				range: positions.range(stmt.start, stmt.end),
				selectionRange: nameRangeOf(positions, stmt.name.start, stmt.name.end),
				children: funChildren(stmt, positions),
			});
		} else if (stmt.kind === 'ClassDecl') {
			const children: DocumentSymbol[] = stmt.methods.map((m) => ({
				name: m.name.text,
				detail: `method(${m.params.map((p) => p.text).join(', ')})`,
				kind: SymbolKind.Method,
				range: positions.range(m.start, m.end),
				selectionRange: nameRangeOf(positions, m.name.start, m.name.end),
			}));
			result.push({
				name: stmt.name.text,
				detail: stmt.superclass ? `class < ${stmt.superclass.token.text}` : 'class',
				kind: SymbolKind.Class,
				range: positions.range(stmt.start, stmt.end),
				selectionRange: nameRangeOf(positions, stmt.name.start, stmt.name.end),
				children,
			});
		}
	}

	return result;
}

function funChildren(stmt: FunDeclStmt, positions: Positions): DocumentSymbol[] {
	// nested declarations inside the function body (one level of value)
	const nested: DocumentSymbol[] = [];
	for (const s of stmt.body.statements) {
		if (s.kind === 'FunDecl') {
			nested.push({
				name: s.name.text,
				detail: 'fun',
				kind: SymbolKind.Function,
				range: positions.range(s.start, s.end),
				selectionRange: nameRangeOf(positions, s.name.start, s.name.end),
			});
		}
	}
	return nested;
}

export function documentSymbols(analysis: Analysis, positions: Positions): DocumentSymbol[] {
	return documentSymbolsFromStatements(analysis.script.statements, positions);
}

/** Workspace symbols from a set of loaded documents. */
export function workspaceSymbols(
	query: string,
	docs: { uri: string; analysis: Analysis; positions: Positions }[],
): SymbolInformation[] {
	const lower = query.toLowerCase();
	const result: SymbolInformation[] = [];

	for (const doc of docs) {
		for (const sym of doc.analysis.symbols) {
			if (sym.kind === DeclKind.Param || sym.kind === DeclKind.Local) {
				continue;
			}
			if (lower && !sym.name.toLowerCase().includes(lower)) {
				continue;
			}
			result.push({
				name: sym.container ? `${sym.container}.${sym.name}` : sym.name,
				kind: kindFor(sym.kind, sym.isConst),
				location: {
					uri: doc.uri,
					range: nameRangeOf(doc.positions, sym.nameToken.start, sym.nameToken.end),
				},
				containerName: sym.container,
			});
		}
	}

	return result;
}
