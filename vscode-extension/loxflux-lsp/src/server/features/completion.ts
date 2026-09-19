/*
 * MIT License
 * Copyright (c) 2025 IMSDCrueoft
 * LoxFlux language server - completion.
 */

import {
	CompletionItem, CompletionItemKind, CompletionItemLabelDetails, CompletionList,
	InsertTextFormat, MarkupContent, MarkupKind, TextEdit,
} from 'vscode-languageserver';
import { TokenType, Token } from '../lexer';
import { ClassInfo, DeclKind } from '../analyzer';
import { Expr } from '../ast';
import { ParsedDocument, DocumentStore } from '../documents';
import { BUILTIN_MODULES, KEYWORD_DOCS } from '../builtins';
import { inferReceiver } from '../infer';
import { walkScript } from '../walker';

function kindForDecl(kind: DeclKind): CompletionItemKind {
	switch (kind) {
		case DeclKind.Function:
		case DeclKind.Method:
			return CompletionItemKind.Function;
		case DeclKind.Class:
			return CompletionItemKind.Class;
		default:
			return kind === DeclKind.Param ? CompletionItemKind.Variable : CompletionItemKind.Variable;
	}
}

function functionItem(name: string, signature: string, doc: string): CompletionItem {
	const labelDetails: CompletionItemLabelDetails = { detail: `(${signature.slice(name.length + 1)}` };
	return {
		label: name,
		labelDetails,
		kind: CompletionItemKind.Function,
		detail: signature,
		documentation: { kind: MarkupKind.Markdown, value: doc } as MarkupContent,
		insertText: name,
	};
}

function constantItem(name: string, signature: string, doc: string): CompletionItem {
	return {
		label: name,
		kind: CompletionItemKind.Constant,
		detail: signature,
		documentation: { kind: MarkupKind.Markdown, value: doc } as MarkupContent,
		insertText: name,
	};
}

/** Item text should replace the partial word being typed. */
function wordRangeAt(doc: ParsedDocument, offset: number): { start: number; end: number } | undefined {
	const text = doc.text;
	let start = offset;
	while (start > 0 && (isWordChar(text[start - 1]) || text[start - 1] === '@')) {
		start--;
	}
	let end = offset;
	while (end < text.length && isWordChar(text[end])) {
		end++;
	}
	if (start === end && start === offset) {
		return undefined;
	}
	return { start, end };
}

function isWordChar(c: string): boolean {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c === '_';
}

/** The token immediately before `offset`, skipping whitespace backwards. */
function tokenBefore(doc: ParsedDocument, offset: number): Token | undefined {
	let best: Token | undefined;
	for (const t of doc.lex.tokens) {
		if (t.type === TokenType.EOF) {
			break;
		}
		if (t.end <= offset) {
			best = t;
		} else {
			break;
		}
	}
	return best;
}

/** Is `offset` directly after a `.`? */
function isAfterDot(doc: ParsedDocument, offset: number): Token | undefined {
	const prev = tokenBefore(doc, offset);
	if (prev && prev.type === TokenType.Dot && prev.end <= offset) {
		return prev;
	}
	return undefined;
}

export function computeCompletions(
	doc: ParsedDocument,
	offset: number,
	store: DocumentStore,
): CompletionList {
	const items: CompletionItem[] = [];

	const dot = isAfterDot(doc, offset);
	if (dot) {
		return CompletionList.create(memberCompletions(doc, dot, store), false);
	}

	// typing `@math` -> the whole `@word` is one (partial) token
	const partial = partialTokenAt(doc, offset);
	if (partial && partial.text.startsWith('@')) {
		for (const mod of Object.values(BUILTIN_MODULES)) {
			items.push({
				label: mod.name,
				kind: CompletionItemKind.Module,
				detail: mod.doc,
				documentation: { kind: MarkupKind.Markdown, value: mod.doc },
				textEdit: TextEdit.replace(
					doc.positions.range(partial.start, offset),
					mod.name,
				),
			});
		}
		return CompletionList.create(items, false);
	}

	// ---- scope completions ----

	const seen = new Set<string>();

	// document symbols visible at the position (locals first, innermost wins)
	const visible = [...doc.analysis.symbols]
		.filter((s) => isVisibleAt(s, offset))
		.sort((a, b) => b.scopeStart - a.scopeStart);
	for (const sym of visible) {
		let detail: string | undefined;
		let docText = sym.doc;
		if (sym.kind === DeclKind.Function || sym.kind === DeclKind.Method) {
			const params = sym.params?.map((p) => p.text).join(', ') ?? '';
			detail = `${sym.name}(${params})`;
			docText = docText ?? '';
			if (sym.container) {
				detail = `${sym.container}.${detail}`;
			}
		}
		const item: CompletionItem = {
			label: sym.name,
			kind: kindForDecl(sym.kind),
			detail,
		};
		if (docText) {
			item.documentation = { kind: MarkupKind.Markdown, value: docText };
		}
		if (sym.isConst) {
			item.kind = CompletionItemKind.Constant;
		}
		if (!seen.has(sym.name)) {
			seen.add(sym.name);
			items.push(item);
		}
	}

	// builtin modules
	for (const mod of Object.values(BUILTIN_MODULES)) {
		if (!seen.has(mod.name)) {
			seen.add(mod.name);
			items.push({
				label: mod.name,
				kind: CompletionItemKind.Module,
				detail: mod.doc,
			});
		}
	}

	// keywords & snippets
	for (const kw of KEYWORD_DOCS) {
		const item: CompletionItem = {
			label: kw.keyword,
			kind: kw.snippet ? CompletionItemKind.Snippet : CompletionItemKind.Keyword,
			detail: firstLine(kw.doc),
		};
		if (kw.snippet) {
			item.insertText = kw.snippet;
			item.insertTextFormat = InsertTextFormat.Snippet;
		}
		items.push(item);
	}

	return CompletionList.create(items, false);
}

function firstLine(text: string): string {
	const idx = text.indexOf('\n');
	return idx >= 0 ? text.slice(0, idx) : text;
}

function isVisibleAt(
	sym: { isGlobal: boolean; nameToken: { start: number }; scopeStart: number; scopeEnd: number },
	offset: number,
): boolean {
	if (sym.isGlobal) {
		return true;
	}
	if (sym.nameToken.start > offset) {
		return false;
	}
	return offset >= sym.scopeStart && offset <= sym.scopeEnd;
}

// ---- member completion --------------------------------------------------

function memberCompletions(
	doc: ParsedDocument,
	dot: Token,
	store: DocumentStore,
): CompletionItem[] {
	const items: CompletionItem[] = [];

	// find the Property node whose dot is this token
	let receiver: Expr | undefined;
	walkScript(doc.analysis.script, {
		expr: (expr) => {
			if (expr.kind === 'Property' && expr.dot.start === dot.start) {
				receiver = expr.object;
			}
		},
	});

	const info = inferReceiver(doc, receiver, store);

	if (info.kind === 'module') {
		const mod = BUILTIN_MODULES[info.moduleKey];
		if (mod) {
			for (const m of mod.members) {
				if (m.kind === 'constant') {
					items.push(constantItem(m.name, m.signature, m.doc));
				} else {
					items.push(functionItem(m.name, m.signature, m.doc));
				}
			}
		}
		return items;
	}

	if (info.kind === 'class') {
		// methods + fields (including inherited ones)
		const seen = new Set<string>();
		let cls: ClassInfo | undefined = info.info;
		while (cls) {
			for (const [name, sym] of cls.methods) {
				if (seen.has(name)) {
					continue;
				}
				seen.add(name);
				const params = sym.params?.map((p) => p.text).join(', ') ?? '';
				items.push(functionItem(name, `${name}(${params})`, sym.doc ?? `Method of ${info.info.name}.`));
			}
			for (const name of cls.fields.keys()) {
				if (seen.has(name)) {
					continue;
				}
				seen.add(name);
				items.push({
					label: name,
					kind: CompletionItemKind.Field,
					detail: `field of ${info.info.name}`,
				});
			}
			cls = cls.superclassName ? doc.analysis.classes.get(cls.superclassName) : undefined;
		}
		return items;
	}

	if (info.kind === 'imports') {
		const moduleDoc = store.loadModule(info.uri, new Set());
		if (moduleDoc) {
			for (const exp of moduleDoc.analysis.exports) {
				if (exp.name.startsWith('<')) {
					continue;
				}
				items.push({
					label: exp.name,
					kind: exp.hint === 'function' ? CompletionItemKind.Function : exp.hint === 'class' ? CompletionItemKind.Class : CompletionItemKind.Variable,
					detail: 'imported',
					documentation: exp.doc ? { kind: MarkupKind.Markdown, value: exp.doc } : undefined,
				});
			}
		}
		return items;
	}

	// unknown receiver: suggest property names seen in this document
	for (const name of doc.analysis.propertyNames.keys()) {
		if (!name) {
			continue;
		}
		items.push({
			label: name,
			kind: CompletionItemKind.Property,
			detail: 'property',
		});
	}
	return items;
}

/** Word (or @word) being typed at `offset`. */
function partialTokenAt(doc: ParsedDocument, offset: number): { start: number; end: number; text: string } | undefined {
	const range = wordRangeAt(doc, offset);
	if (!range) {
		return undefined;
	}
	const text = doc.text.slice(range.start, offset);
	if (!text) {
		return undefined;
	}
	return { start: range.start, end: offset, text };
}
