/*
 * MIT License
 * Copyright (c) 2025 IMSDCrueoft
 * LoxFlux language server - document store with import resolution.
 */

import * as fs from 'fs';
import * as path from 'path';
import { URI } from 'vscode-uri';
import { TextDocument } from 'vscode-languageserver-textdocument';
import { lex } from './lexer';
import { parse } from './parser';
import { Analyzer, Analysis, LintSettings, DEFAULT_LINT_SETTINGS } from './analyzer';
import { Positions } from './positions';

export interface ParsedDocument {
	uri: string;
	fsPath: string;
	text: string;
	version: number | undefined;
	/** true when the text came from an open editor buffer */
	isOpen: boolean;
	textDocument: TextDocument;
	lex: ReturnType<typeof lex>;
	analysis: Analysis;
	positions: Positions;
	lint: LintSettings;
	/** epoch ms when disk content was read (for cache validation) */
	diskMtimeMs: number | undefined;
}

export class DocumentStore {
	private readonly docs = new Map<string, ParsedDocument>();
	private lintSettings: LintSettings = DEFAULT_LINT_SETTINGS;
	private workspaceFolders: string[] = [];

	setLintSettings(settings: LintSettings): void {
		this.lintSettings = settings;
	}

	setWorkspaceFolders(folders: string[]): void {
		this.workspaceFolders = folders;
	}

	get(uri: string): ParsedDocument | undefined {
		return this.docs.get(uri);
	}

	allOpen(): ParsedDocument[] {
		return [...this.docs.values()].filter((d) => d.isOpen);
	}

	open(uri: string, languageId: string, version: number, text: string): ParsedDocument {
		const doc = this.build(uri, text, version, true, undefined);
		this.docs.set(uri, doc);
		void languageId;
		return doc;
	}

	change(uri: string, version: number, text: string): ParsedDocument {
		const doc = this.build(uri, text, version, true, undefined);
		this.docs.set(uri, doc);
		return doc;
	}

	close(uri: string): void {
		this.docs.delete(uri);
	}

	/** Get a document for analysis; reads from disk when not open. */
	load(uri: string): ParsedDocument {
		const existing = this.docs.get(uri);
		if (existing && existing.isOpen) {
			return existing;
		}

		const fsPath = URI.parse(uri).fsPath;
		let mtime: number | undefined;
		try {
			mtime = fs.statSync(fsPath).mtimeMs;
		} catch {
			mtime = undefined;
		}
		if (existing && !existing.isOpen && existing.diskMtimeMs === mtime) {
			return existing;
		}

		let text = '';
		try {
			text = fs.readFileSync(fsPath, 'utf8');
		} catch {
			text = '';
		}
		const doc = this.build(uri, text, undefined, false, mtime);
		this.docs.set(uri, doc);
		return doc;
	}

	private build(uri: string, text: string, version: number | undefined, isOpen: boolean, diskMtimeMs: number | undefined): ParsedDocument {
		const fsPath = URI.parse(uri).fsPath;
		const lexed = lex(text);
		const script = parse(lexed.tokens);
		script.comments = lexed.comments;
		const analysis = new Analyzer(script, text, this.lintSettings).analyze();
		return {
			uri,
			fsPath,
			text,
			version,
			isOpen,
			textDocument: TextDocument.create(uri, 'loxflux', version ?? 0, text),
			lex: lexed,
			analysis,
			positions: new Positions(text),
			lint: this.lintSettings,
			diskMtimeMs,
		};
	}

	// ---- import resolution ---------------------------------------------

	/** Resolve an import path literal from a document to a file URI. */
	resolveImport(fromDoc: ParsedDocument, importPath: string): string | undefined {
		const candidates: string[] = [];

		const fromDir = path.dirname(fromDoc.fsPath);
		candidates.push(path.resolve(fromDir, importPath));

		// also try workspace roots (the interpreter resolves relative to CWD)
		for (const root of this.workspaceFolders) {
			candidates.push(path.resolve(root, importPath));
		}

		for (const candidate of candidates) {
			if (this.existsAsScript(candidate)) {
				return URI.file(path.normalize(candidate)).toString();
			}
			// try with well-known extensions if missing
			if (!path.extname(candidate)) {
				for (const ext of ['.lfx', '.lox']) {
					const withExt = candidate + ext;
					if (this.existsAsScript(withExt)) {
						return URI.file(path.normalize(withExt)).toString();
					}
				}
			}
		}
		return undefined;
	}

	private existsAsScript(fsPath: string): boolean {
		try {
			return fs.statSync(fsPath).isFile();
		} catch {
			return false;
		}
	}

	/** Load a module document (from import) with cycle protection. */
	loadModule(uri: string, visiting: Set<string>): ParsedDocument | undefined {
		if (visiting.has(uri)) {
			return undefined;
		}
		visiting.add(uri);
		try {
			return this.load(uri);
		} finally {
			visiting.delete(uri);
		}
	}
}
