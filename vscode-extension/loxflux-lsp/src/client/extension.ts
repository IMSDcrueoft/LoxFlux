/*
 * MIT License
 * Copyright (c) 2025 IMSDCrueoft
 * LoxFlux language server - VS Code client.
 */

import * as path from 'path';

import { workspace, ExtensionContext } from 'vscode';

import {
	LanguageClient,
	LanguageClientOptions,
	ServerOptions,
	TransportKind,
} from 'vscode-languageclient/node';

let client: LanguageClient | undefined;

export function activate(context: ExtensionContext): void {
	// The server is implemented in node
	const serverModule = context.asAbsolutePath(path.join('out', 'server.js'));

	const debugOptions = { execArgv: ['--nolazy', '--inspect=6019'] };

	const serverOptions: ServerOptions = {
		run: { module: serverModule, transport: TransportKind.ipc },
		debug: { module: serverModule, transport: TransportKind.ipc, options: debugOptions },
	};

	const clientOptions: LanguageClientOptions = {
		documentSelector: [
			{ language: 'loxflux', scheme: 'file' },
			{ language: 'loxflux', scheme: 'untitled' },
		],
		synchronize: {
			// notify the server about configuration changes
			configurationSection: ['loxflux'],
			fileEvents: workspace.createFileSystemWatcher('**/*.lfx'),
		},
	};

	client = new LanguageClient(
		'loxflux',
		'LoxFlux',
		serverOptions,
		clientOptions,
	);

	client.start();
	context.subscriptions.push({
		dispose: () => {
			void client?.stop();
		},
	});
}

export function deactivate(): Thenable<void> | undefined {
	if (!client) {
		return undefined;
	}
	return client.stop();
}
