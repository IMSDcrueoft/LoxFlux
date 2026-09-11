/*
 * MIT License
 * Copyright (c) 2025 IMSDCrueoft
 * LoxFlux language server - definition, references and rename.
 */

import {
	Location, Range, TextEdit, WorkspaceEdit,
} from 'vscode-languageserver';
import { Expr } from '../ast';
import { TokenType, Token } from '../lexer';
import { Symbol } from '../analyzer';
import { ParsedDocument, DocumentStore } from '../documents';
import { Positions } from '../positions';
import { walkScript } from '../walker';
import { inferReceiver } from '../infer';

function rangeOf(positions: Positions, start: number, end: number): Range {
	return positions.range(start, end);
}

/** The token covering `offset`, if any. */
export function tokenAtOffset(doc: ParsedDocument, offset: number): Token | undefined {
	for (const t of doc.lex.tokens) {
		if (offset >= t.start && offset < t.end) {
			return t;
		}
	}
	return undefined;
}

/** Resolve the symbol declared or referenced at an offset (document-local). */
export function symbolAtOffset(doc: ParsedDocument, offset: number): Symbol | undefined {
	// declaration name tokens
	for (const sym of doc.analysis.symbols) {
		if (offset >= sym.nameToken.start && offset < sym.nameToken.end) {
			return sym;
		}
	}
	// usages
	for (const usage of doc.analysis.usages) {
		if (offset >= usage.token.start && offset < usage.token.end) {
			return usage.resolved;
		}
	}
	return undefined;
}

function findPropertyAtOffset(doc: ParsedDocument, offset: number): { name: { start: number; end: number; text: string }; object: Expr } | undefined {
	let found: { name: { start: number; end: number; text: string }; object: Expr } | undefined;
	walkScript(doc.analysis.script, {
		expr: (expr) => {
			if (expr.kind === 'Property' && offset >= expr.name.start && offset < expr.name.end) {
				found = { name: expr.name, object: expr.object };
			}
		},
	});
	return found;
}

// ---- definition --------------------------------------------------------

export function definitionAt(
	doc: ParsedDocument,
	offset: number,
	store: DocumentStore,
): Location | undefined {
	const token = tokenAtOffset(doc, offset);
	if (!token) {
		return undefined;
	}

	// import path literal -> module file
	if (token.type === TokenType.String || token.type === TokenType.StringEscape) {
		const importInfo = doc.analysis.imports.find((i) => offset >= i.pathStart && offset < i.pathEnd);
		if (importInfo && importInfo.path) {
			const uri = store.resolveImport(doc, importInfo.path);
			if (uri) {
				return Location.create(uri, Range.create(0, 0, 0, 0));
			}
		}
		return undefined;
	}

	// document-local symbol
	const symbol = symbolAtOffset(doc, offset);
	if (symbol) {
		return Location.create(doc.uri, rangeOf(doc.positions, symbol.nameToken.start, symbol.nameToken.end));
	}

	// property -> method/field of the receiver class, or export of an imported module
	const prop = findPropertyAtOffset(doc, offset);
	if (prop) {
		const receiver = inferReceiver(doc, prop.object, store);
		if (receiver.kind === 'class') {
			const method = receiver.info.methods.get(prop.name.text);
			if (method) {
				return Location.create(receiver.uri, rangeOf(doc.positions, method.nameToken.start, method.nameToken.end));
			}
			const field = receiver.info.fields.get(prop.name.text);
			if (field) {
				return Location.create(receiver.uri, rangeOf(doc.positions, field.start, field.end));
			}
		} else if (receiver.kind === 'imports') {
			const target = exportLocation(receiver.uri, prop.name.text, store);
			if (target) {
				return target;
			}
		}
		return undefined;
	}

	// unresolved identifier -> exported from one of the imports
	const imported = findExportedSymbol(doc, token.text, store);
	if (imported) {
		return Location.create(imported.uri, rangeOf(imported.doc.positions, imported.targetStart, imported.targetEnd));
	}

	return undefined;
}

/** Location of an exported name inside a module document. */
function exportLocation(moduleUri: string, name: string, store: DocumentStore): Location | undefined {
	const moduleDoc = store.loadModule(moduleUri, new Set());
	if (!moduleDoc) {
		return undefined;
	}
	const exported = moduleDoc.analysis.exports.find((e) => e.name === name);
	if (exported) {
		return Location.create(moduleUri, rangeOf(moduleDoc.positions, exported.targetStart, exported.targetEnd));
	}
	const sym = moduleDoc.analysis.globals.find((s) => s.name === name);
	if (sym) {
		return Location.create(moduleUri, rangeOf(moduleDoc.positions, sym.nameToken.start, sym.nameToken.end));
	}
	return undefined;
}

export function findExportedSymbol(
	doc: ParsedDocument,
	name: string,
	store: DocumentStore,
): { uri: string; doc: ParsedDocument; targetStart: number; targetEnd: number } | undefined {
	const visiting = new Set<string>();
	for (const imp of doc.analysis.imports) {
		if (!imp.path) {
			continue;
		}
		const uri = store.resolveImport(doc, imp.path);
		if (!uri) {
			continue;
		}
		const moduleDoc = store.loadModule(uri, visiting);
		if (!moduleDoc) {
			continue;
		}
		const exported = moduleDoc.analysis.exports.find((e) => e.name === name);
		if (exported) {
			return {
				uri,
				doc: moduleDoc,
				targetStart: exported.targetStart,
				targetEnd: exported.targetEnd,
			};
		}
		// also search the module's top-level symbols as a fallback
		const sym = moduleDoc.analysis.globals.find((s) => s.name === name);
		if (sym) {
			return { uri, doc: moduleDoc, targetStart: sym.nameToken.start, targetEnd: sym.nameToken.end };
		}
	}
	return undefined;
}

// ---- references --------------------------------------------------------

export function referencesAt(
	doc: ParsedDocument,
	offset: number,
	includeDeclaration: boolean,
): Location[] {
	const symbol = symbolAtOffset(doc, offset);
	if (!symbol) {
		return [];
	}

	const locations: Location[] = [];
	if (includeDeclaration) {
		locations.push(Location.create(doc.uri, rangeOf(doc.positions, symbol.nameToken.start, symbol.nameToken.end)));
	}
	for (const ref of symbol.references) {
		locations.push(Location.create(doc.uri, rangeOf(doc.positions, ref.start, ref.end)));
	}
	return locations;
}

// ---- rename ------------------------------------------------------------

export function prepareRename(doc: ParsedDocument, offset: number): Range | undefined {
	const symbol = symbolAtOffset(doc, offset);
	if (!symbol) {
		return undefined;
	}
	return rangeOf(doc.positions, symbol.nameToken.start, symbol.nameToken.end);
}

export function renameSymbol(
	doc: ParsedDocument,
	offset: number,
	newName: string,
): WorkspaceEdit | undefined {
	const symbol = symbolAtOffset(doc, offset);
	if (!symbol) {
		return undefined;
	}

	const edits: TextEdit[] = [];

	// declaration
	edits.push(TextEdit.replace(
		rangeOf(doc.positions, symbol.nameToken.start, symbol.nameToken.end),
		newName,
	));

	// all references in this document
	for (const ref of symbol.references) {
		edits.push(TextEdit.replace(rangeOf(doc.positions, ref.start, ref.end), newName));
	}

	return { changes: { [doc.uri]: edits } };
}
