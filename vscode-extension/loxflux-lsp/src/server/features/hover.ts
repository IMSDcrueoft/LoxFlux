/*
 * MIT License
 * Copyright (c) 2025 IMSDCrueoft
 * LoxFlux language server - hover.
 */

import { Hover, MarkupContent, MarkupKind } from 'vscode-languageserver';
import { TokenType, Token } from '../lexer';
import { Expr } from '../ast';
import { DeclKind, declKindLabel } from '../analyzer';
import { ParsedDocument, DocumentStore } from '../documents';
import { KEYWORD_DOCS, BUILTIN_MODULES } from '../builtins';
import { tokenAtOffset, symbolAtOffset, findExportedSymbol } from './navigation';
import { inferReceiver } from '../infer';
import { walkScript } from '../walker';

function markdown(value: string): MarkupContent {
	return { kind: MarkupKind.Markdown, value };
}

function symbolSignature(symbol: { name: string; kind: DeclKind; isConst: boolean; params?: { text: string }[] | undefined }): string {
	const params = symbol.params && symbol.params.length > 0
		? symbol.params.map((p) => p.text).join(', ')
		: '';
	switch (symbol.kind) {
		case DeclKind.Function:
		case DeclKind.Method:
			return `${symbol.name}(${params})`;
		case DeclKind.Class:
			return `class ${symbol.name}`;
		default:
			return symbol.isConst ? `const ${symbol.name}` : `var ${symbol.name}`;
	}
}

function symbolHoverMarkdown(symbol: { name: string; kind: DeclKind; isConst: boolean; params?: { text: string }[] | undefined; doc?: string; container?: string }): string {
	const label = declKindLabel(symbol.kind);
	const containerPrefix = symbol.container ? `${symbol.container}.` : '';
	const parts: string[] = [];
	parts.push(`\`\`\`lox\n${containerPrefix}${symbolSignature(symbol)}\n\`\`\``);
	parts.push(`*${label.charAt(0).toUpperCase()}${label.slice(1)}*`);
	if (symbol.doc) {
		parts.push(symbol.doc);
	}
	return parts.join('\n\n');
}

export function hoverAt(doc: ParsedDocument, offset: number, store: DocumentStore): Hover | undefined {
	const token = tokenAtOffset(doc, offset);
	if (!token) {
		return undefined;
	}

	switch (token.type) {
		case TokenType.Identifier: {
			const symbol = symbolAtOffset(doc, offset);
			if (symbol) {
				return { contents: markdown(symbolHoverMarkdown(symbol)) };
			}
			// exported symbol from an imported module?
			const imported = findExportedSymbol(doc, token.text, store);
			if (imported) {
				const exported = imported.doc.analysis.exports.find((e) => e.name === token.text);
				if (exported) {
					const hintLabel = exported.hint.charAt(0).toUpperCase() + exported.hint.slice(1);
					const parts = [`\`\`\`lox\n${exported.name}\n\`\`\``, `*${hintLabel}* (imported)`];
					if (exported.doc) {
						parts.push(exported.doc);
					}
					return { contents: markdown(parts.join('\n\n')) };
				}
			}
			return undefined;
		}

		case TokenType.ModuleMath:
		case TokenType.ModuleArray:
		case TokenType.ModuleObject:
		case TokenType.ModuleString:
		case TokenType.ModuleTime:
		case TokenType.ModuleCtor:
		case TokenType.ModuleSys: {
			const mod = BUILTIN_MODULES[token.text];
			if (!mod) {
				return undefined;
			}
			const memberLines = mod.members.map((m) => `- \`${m.signature}\``).join('\n');
			return {
				contents: markdown(`**${mod.name}** builtin module\n\n${mod.doc}\n\n${memberLines}`),
			};
		}

		case TokenType.String:
		case TokenType.StringEscape: {
			const importInfo = doc.analysis.imports.find((i) => offset >= i.pathStart && offset < i.pathEnd);
			if (importInfo && importInfo.path) {
				const uri = store.resolveImport(doc, importInfo.path);
				const status = uri ? 'resolves to `' + uri.replace('file://', '') + '`' : '*cannot resolve module*';
				return { contents: markdown(`**import** \`${importInfo.path}\`\n\n${status}`) };
			}
			return undefined;
		}

		default: {
			// keyword documentation
			const keyword = KEYWORD_DOCS.find((k) => k.keyword === token.text);
			if (keyword && isKeywordToken(token.type)) {
				return {
					contents: markdown(`\`\`\`lox\n${keyword.keyword}\n\`\`\`\n\n${keyword.doc}`),
				};
			}

			// property hover (method/field of a class, or export of an import)
			const prop = findPropertyToken(doc, offset);
			if (prop) {
				const receiver = inferReceiver(doc, prop.object, store);
				if (receiver.kind === 'class') {
					const method = receiver.info.methods.get(prop.name.text);
					if (method) {
						return { contents: markdown(symbolHoverMarkdown(method)) };
					}
					const field = receiver.info.fields.get(prop.name.text);
					if (field) {
						const parts = [`\`\`\`lox\n${prop.name.text}\n\`\`\``, '*Field*'];
						return { contents: markdown(parts.join('\n\n')) };
					}
				} else if (receiver.kind === 'imports') {
					const moduleDoc = store.loadModule(receiver.uri, new Set());
					const exported = moduleDoc?.analysis.exports.find((e) => e.name === prop.name.text);
					if (exported) {
						const hintLabel = exported.hint.charAt(0).toUpperCase() + exported.hint.slice(1);
						const parts = [`\`\`\`lox\n${exported.name}\n\`\`\``, `*${hintLabel}* (imported)`];
						if (exported.doc) {
							parts.push(exported.doc);
						}
						return { contents: markdown(parts.join('\n\n')) };
					}
				} else if (receiver.kind === 'module') {
					const mod = BUILTIN_MODULES[receiver.moduleKey];
					const member = mod?.members.find((m) => m.name === prop.name.text);
					if (member) {
						const label = member.kind === 'constant' ? 'Constant' : member.kind === 'class' ? 'Class' : 'Function';
						const parts = [`\`\`\`lox\n${member.signature}\n\`\`\``, `*${label}* (builtin)`];
						if (member.doc) {
							parts.push(member.doc);
						}
						return { contents: markdown(parts.join('\n\n')) };
					}
				}
				return undefined;
			}
			return undefined;
		}
	}
}

const KEYWORD_TOKEN_TYPES = new Set<number>([
	TokenType.Var, TokenType.Const, TokenType.Fun, TokenType.Class, TokenType.If, TokenType.Else,
	TokenType.Branch, TokenType.None, TokenType.While, TokenType.Do, TokenType.For, TokenType.Break,
	TokenType.Continue, TokenType.Return, TokenType.Throw, TokenType.Export, TokenType.Import,
	TokenType.Lambda, TokenType.This, TokenType.Super, TokenType.TypeOf, TokenType.InstanceOf,
	TokenType.And, TokenType.Or, TokenType.Nil, TokenType.True, TokenType.False, TokenType.Print,
]);

function isKeywordToken(type: TokenType): boolean {
	return KEYWORD_TOKEN_TYPES.has(type);
}

function findPropertyToken(doc: ParsedDocument, offset: number): { name: Token; object: Expr } | undefined {
	let found: { name: Token; object: Expr } | undefined;
	walkScript(doc.analysis.script, {
		expr: (expr) => {
			if (expr.kind === 'Property' && offset >= expr.name.start && offset < expr.name.end) {
				found = { name: expr.name, object: expr.object };
			}
		},
	});
	return found;
}
