/*
 * MIT License
 * Copyright (c) 2025 IMSDCrueoft
 * LoxFlux language server - main entry.
 */

import * as fs from 'fs';
import * as path from 'path';

import {
	InitializeParams,
	InitializeResult,
	TextDocumentSyncKind,
	CompletionItem,
	CompletionList,
	Diagnostic,
	DiagnosticSeverity,
	DidChangeConfigurationNotification,
	Hover,
	Location,
	Range,
	RelatedFullDocumentDiagnosticReport,
	SemanticTokens,
	SemanticTokensParams,
	SemanticTokensRangeParams,
	WorkspaceEdit,
	createConnection,
	ProposedFeatures,
	TextDocuments,
} from 'vscode-languageserver/node';

import { TextDocument } from 'vscode-languageserver-textdocument';
import { URI } from 'vscode-uri';

import { DocumentStore, ParsedDocument } from './documents';
import { LintSettings } from './analyzer';
import { computeSemanticTokens, SEMANTIC_TOKEN_LEGEND } from './features/semanticTokens';
import { documentSymbols, workspaceSymbols } from './features/symbols';
import { hoverAt } from './features/hover';
import { computeCompletions } from './features/completion';
import { definitionAt, referencesAt, prepareRename, renameSymbol } from './features/navigation';

const connection = createConnection(ProposedFeatures.all);

const documents: TextDocuments<TextDocument> = new TextDocuments(TextDocument);
documents.listen(connection);

const store = new DocumentStore();

// uris of every .lfx/.lox file we have seen, for workspace symbol search
const workspaceFileUris = new Set<string>();

connection.onInitialize((params: InitializeParams): InitializeResult => {
	const folders = (params.workspaceFolders ?? []).map((f) => URI.parse(f.uri).fsPath);
	store.setWorkspaceFolders(folders);

	if (folders.length > 0) {
		scanWorkspace(folders);
	}

	return {
		capabilities: {
			textDocumentSync: TextDocumentSyncKind.Incremental,
			hoverProvider: true,
			completionProvider: {
				triggerCharacters: ['.', '@'],
				resolveProvider: true,
			},
			definitionProvider: true,
			referencesProvider: true,
			documentSymbolProvider: true,
			workspaceSymbolProvider: true,
			renameProvider: {
				prepareProvider: true,
			},
			semanticTokensProvider: {
				legend: SEMANTIC_TOKEN_LEGEND,
				full: true,
				range: true,
			},
			diagnosticProvider: {
				interFileDependencies: false,
				workspaceDiagnostics: false,
			},
		},
	};
});

connection.onInitialized(() => {
	// pull lint configuration changes
	connection.client.register(DidChangeConfigurationNotification.type, undefined);
});

function scanWorkspace(folders: string[]): void {
	const skip = new Set(['node_modules', '.git', '.svn', 'out', 'dist']);
	for (const root of folders) {
		const visit = (dir: string, depth: number): void => {
			if (depth > 8) {
				return;
			}
			let entries: fs.Dirent[];
			try {
				entries = fs.readdirSync(dir, { withFileTypes: true });
			} catch {
				return;
			}
			for (const entry of entries) {
				if (skip.has(entry.name)) {
					continue;
				}
				const full = path.join(dir, entry.name);
				if (entry.isDirectory()) {
					visit(full, depth + 1);
				} else if (entry.name.endsWith('.lfx') || entry.name.endsWith('.lox')) {
					workspaceFileUris.add(URI.file(full).toString());
				}
			}
		};
		visit(root, 0);
	}
}

// ---- configuration -----------------------------------------------------

connection.onDidChangeConfiguration(() => {
	store.setLintSettings(getLintSettings());
	// revalidate all open documents with the new settings
	documents.all().forEach(validateTextDocument);
});

function getLintSettings(): LintSettings {
	return connection.workspace.getConfiguration('loxflux.lint') as unknown as LintSettings;
}

// ---- document helpers --------------------------------------------------

/** Sync the store with the open editor buffer (or fall back to disk). */
function getDoc(uri: string): ParsedDocument | undefined {
	const open = documents.get(uri);
	if (open) {
		return store.open(uri, open.languageId, open.version, open.getText());
	}
	if (workspaceFileUris.has(uri)) {
		return store.load(uri);
	}
	return undefined;
}

// ---- diagnostics -------------------------------------------------------

documents.onDidOpen((event) => {
	workspaceFileUris.add(event.document.uri);
	validateTextDocument(event.document);
});

documents.onDidChangeContent((event) => {
	validateTextDocument(event.document);
});

documents.onDidClose((event) => {
	connection.sendDiagnostics({ uri: event.document.uri, diagnostics: [] });
});

function severityOf(s: 'error' | 'warning' | 'info' | 'hint'): DiagnosticSeverity {
	switch (s) {
		case 'error': return DiagnosticSeverity.Error;
		case 'warning': return DiagnosticSeverity.Warning;
		case 'info': return DiagnosticSeverity.Information;
		default: return DiagnosticSeverity.Hint;
	}
}

function toLspDiagnostics(doc: ParsedDocument): Diagnostic[] {
	const diagnostics: Diagnostic[] = [];
	for (const d of doc.analysis.diagnostics) {
		diagnostics.push({
			severity: severityOf(d.severity),
			range: doc.positions.range(d.start, d.end),
			message: d.message,
			source: 'loxflux',
			code: d.code,
		});
	}
	// import resolution warnings
	for (const imp of doc.analysis.imports) {
		if (!imp.path) {
			continue;
		}
		const resolved = store.resolveImport(doc, imp.path);
		if (!resolved) {
			diagnostics.push({
				severity: DiagnosticSeverity.Warning,
				range: doc.positions.range(imp.pathStart, imp.pathEnd),
				message: `Cannot find module: ${imp.path}`,
				source: 'loxflux',
				code: 'import',
			});
		}
	}
	return diagnostics;
}

function validateTextDocument(textDocument: TextDocument): void {
	const doc = store.open(textDocument.uri, textDocument.languageId, textDocument.version, textDocument.getText());
	connection.sendDiagnostics({
		uri: textDocument.uri,
		diagnostics: toLspDiagnostics(doc),
	});
}

// ---- hover -------------------------------------------------------------

connection.onHover((params): Hover | undefined => {
	const doc = getDoc(params.textDocument.uri);
	if (!doc) {
		return undefined;
	}
	const offset = doc.textDocument.offsetAt(params.position);
	return hoverAt(doc, offset, store);
});

// ---- completion --------------------------------------------------------

connection.onCompletion((params): CompletionList => {
	const doc = getDoc(params.textDocument.uri);
	if (!doc) {
		return CompletionList.create([], false);
	}
	const offset = doc.textDocument.offsetAt(params.position);
	return computeCompletions(doc, offset, store);
});

connection.onCompletionResolve((item: CompletionItem): CompletionItem => {
	return item;
});

// ---- definition / references ------------------------------------------

connection.onDefinition((params) => {
	const doc = getDoc(params.textDocument.uri);
	if (!doc) {
		return undefined;
	}
	const offset = doc.textDocument.offsetAt(params.position);
	return definitionAt(doc, offset, store);
});

connection.onReferences((params): Location[] => {
	const doc = getDoc(params.textDocument.uri);
	if (!doc) {
		return [];
	}
	const offset = doc.textDocument.offsetAt(params.position);
	return referencesAt(doc, offset, params.context.includeDeclaration);
});

// ---- rename ------------------------------------------------------------

connection.onPrepareRename((params): { range: Range; placeholder: string } | undefined => {
	const doc = getDoc(params.textDocument.uri);
	if (!doc) {
		return undefined;
	}
	const range = prepareRename(doc, doc.textDocument.offsetAt(params.position));
	if (!range) {
		return undefined;
	}
	const placeholder = doc.textDocument.getText(range);
	return { range, placeholder };
});

connection.onRenameRequest((params): WorkspaceEdit | undefined => {
	const doc = getDoc(params.textDocument.uri);
	if (!doc) {
		return undefined;
	}
	const offset = doc.textDocument.offsetAt(params.position);
	return renameSymbol(doc, offset, params.newName);
});

// ---- symbols -----------------------------------------------------------

connection.onDocumentSymbol((params) => {
	const doc = getDoc(params.textDocument.uri);
	if (!doc) {
		return [];
	}
	return documentSymbols(doc.analysis, doc.positions);
});

connection.onWorkspaceSymbol((params) => {
	const docs: ParsedDocument[] = [];

	for (const uri of workspaceFileUris) {
		const doc = getDoc(uri);
		if (doc && doc.text.length > 0) {
			docs.push(doc);
		}
	}

	return workspaceSymbols(params.query ?? '', docs);
});

// ---- semantic tokens ---------------------------------------------------

connection.languages.semanticTokens.on((params: SemanticTokensParams): SemanticTokens => {
	const doc = getDoc(params.textDocument.uri);
	if (!doc) {
		return { data: [] };
	}
	return computeSemanticTokens(doc.analysis, doc.positions);
});

connection.languages.semanticTokens.onRange((params: SemanticTokensRangeParams): SemanticTokens => {
	const doc = getDoc(params.textDocument.uri);
	if (!doc) {
		return { data: [] };
	}
	const full = computeSemanticTokens(doc.analysis, doc.positions);
	// filter to the requested range
	const tokens = full.data;
	const out: number[] = [];
	let line = 0;
	let character = 0;
	let i = 0;
	while (i < tokens.length) {
		const deltaLine = tokens[i];
		const deltaChar = tokens[i + 1];
		const length = tokens[i + 2];
		line += deltaLine;
		character = deltaLine === 0 ? character + deltaChar : deltaChar;
		const start = { line, character };
		const within = start.line >= rangeLine(params.range.start.line)
			&& start.line <= params.range.end.line
			&& !(start.line === params.range.end.line && start.character > params.range.end.character);
		if (within) {
			out.push(deltaLine, deltaChar, length, tokens[i + 3], tokens[i + 4]);
		}
		i += 5;
	}
	return { data: out };
});

function rangeLine(line: number): number {
	return line;
}

// ---- pull diagnostics ---------------------------------------------------

connection.languages.diagnostics.on((params): RelatedFullDocumentDiagnosticReport => {
	const doc = getDoc(params.textDocument.uri);
	const items = doc ? toLspDiagnostics(doc) : [];
	return { kind: 'full', items };
});

connection.listen();
