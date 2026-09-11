/*
 * LSP protocol smoke test: spawns the bundled server over stdio and runs
 * initialize -> didOpen -> diagnostics -> hover -> completion -> definition.
 */
import { spawn } from 'child_process';
import { createInterface } from 'readline';
import { fileURLToPath } from 'url';
import path from 'path';
import fs from 'fs';
import os from 'os';

const here = path.dirname(fileURLToPath(import.meta.url));
const serverJs = path.join(here, '..', '..', 'out', 'server.js');

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'loxflux-lsp-test-'));
const mainFile = path.join(tmp, 'main.lfx');
const libFile = path.join(tmp, 'lib.lfx');

fs.writeFileSync(libFile, `// A useful library.\nconst PI = 3.14;\nfun double(a) { return a * 2; }\nexport { "pi": PI, "double": double };\n`);
fs.writeFileSync(mainFile, `var lib = import "./lib.lfx";\nprint lib.pi;\nvar x = lib.double(21);\nclass Animal {\n\tinit(name) {\n\t\tthis.name = name;\n\t}\n\tspeak() {\n\t\tprint this.name;\n\t}\n}\nvar a = Animal("dog");\na.speak();\n`);

const proc = spawn(process.execPath, [serverJs, '--stdio'], { stdio: ['pipe', 'pipe', 'pipe'] });

let buffer = Buffer.alloc(0);
let pending = [];
let messageId = 0;
const waiting = new Map();

proc.stdout.on('data', (chunk) => {
	buffer = Buffer.concat([buffer, chunk]);
	while (true) {
		const headerEnd = buffer.indexOf('\r\n\r\n');
		if (headerEnd < 0) break;
		const header = buffer.slice(0, headerEnd).toString('utf8');
		const match = /Content-Length: (\d+)/.exec(header);
		if (!match) break;
		const length = parseInt(match[1], 10);
		const bodyStart = headerEnd + 4;
		if (buffer.length < bodyStart + length) break;
		const body = buffer.slice(bodyStart, bodyStart + length).toString('utf8');
		buffer = buffer.slice(bodyStart + length);
		const msg = JSON.parse(body);
		if (msg.id !== undefined && waiting.has(msg.id)) {
			const resolve = waiting.get(msg.id);
			waiting.delete(msg.id);
			resolve(msg.result);
		} else {
			pending.push(msg);
		}
	}
});

proc.stderr.on('data', (d) => process.stderr.write(d));

function send(method, params) {
	const msg = JSON.stringify({ jsonrpc: '2.0', id: ++messageId, method, params });
	proc.stdin.write(`Content-Length: ${Buffer.byteLength(msg)}\r\n\r\n${msg}`);
	return messageId;
}

function notify(method, params) {
	const msg = JSON.stringify({ jsonrpc: '2.0', method, params });
	proc.stdin.write(`Content-Length: ${Buffer.byteLength(msg)}\r\n\r\n${msg}`);
}

function request(method, params) {
	return new Promise((resolve, reject) => {
		const id = send(method, params);
		waiting.set(id, resolve);
		setTimeout(() => reject(new Error(`timeout waiting for ${method}`)), 5000);
	});
}

function sleep(ms) {
	return new Promise((r) => setTimeout(r, ms));
}

let failures = 0;
function check(name, cond) {
	if (cond) {
		console.log(`  ok - ${name}`);
	} else {
		failures++;
		console.error(`  FAIL - ${name}`);
	}
}

async function main() {
	const init = await request('initialize', {
		processId: process.pid,
		rootUri: 'file://' + tmp,
		workspaceFolders: [{ uri: 'file://' + tmp, name: 'test' }],
		capabilities: {},
	});

	check('server announces hover capability', init.capabilities.hoverProvider === true);
	check('server announces semantic tokens', init.capabilities.semanticTokensProvider !== undefined);

	notify('initialized', {});

	const text = fs.readFileSync(mainFile, 'utf8');
	notify('textDocument/didOpen', {
		textDocument: { uri: 'file://' + mainFile, languageId: 'loxflux', version: 1, text },
	});

	await sleep(300);

	const diags = pending.filter((m) => m.method === 'textDocument/publishDiagnostics');
	const mainDiags = diags.find((d) => d.params.uri === 'file://' + mainFile);
	check('publishes diagnostics without syntax errors', mainDiags && mainDiags.params.diagnostics.length === 0);

	// hover over `double` property of the imported module (line 2: `var x = lib.double(21);`)
	const hover = await request('textDocument/hover', {
		textDocument: { uri: 'file://' + mainFile },
		position: { line: 2, character: 14 },
	});
	check('hover resolves imported function', hover && hover.contents && String(hover.contents.value).includes('double'));

	// completion after `a.` (line 12: `a.speak();` -> after dot col 2)
	const completion = await request('textDocument/completion', {
		textDocument: { uri: 'file://' + mainFile },
		position: { line: 12, character: 2 },
	});
	const labels = (completion?.items ?? []).map((i) => i.label);
	check('member completion offers methods', labels.includes('speak') && labels.includes('init'));

	// scope completion (line 3 start)
	const scope = await request('textDocument/completion', {
		textDocument: { uri: 'file://' + mainFile },
		position: { line: 3, character: 0 },
	});
	const scopeLabels = (scope?.items ?? []).map((i) => i.label);
	check('scope completion offers globals & modules', scopeLabels.includes('x') && scopeLabels.includes('@math'));

	// definition of double -> lib.lfx (line 2 col 14 = `double`)
	const def = await request('textDocument/definition', {
		textDocument: { uri: 'file://' + mainFile },
		position: { line: 2, character: 14 },
	});
	check('go-to-definition jumps to module file', def && def.uri && def.uri.endsWith('lib.lfx'));

	// document symbols
	const symbols = await request('textDocument/documentSymbol', {
		textDocument: { uri: 'file://' + mainFile },
	});
	const names = symbols.map((s) => s.name);
	check('document symbols outline', names.includes('Animal') && names.includes('x') && names.includes('lib'));

	// semantic tokens
	const sem = await request('textDocument/semanticTokens/full', {
		textDocument: { uri: 'file://' + mainFile },
	});
	check('semantic tokens returned', Array.isArray(sem.data) && sem.data.length > 0);

	// references of `a` (declaration line 11 + usage line 12)
	const refs = await request('textDocument/references', {
		textDocument: { uri: 'file://' + mainFile },
		position: { line: 11, character: 4 },
		context: { includeDeclaration: true },
	});
	check('find references works', Array.isArray(refs) && refs.length >= 2);

	// rename local variable x (line 2: `var x = ...` -> col 4)
	const prep = await request('textDocument/prepareRename', {
		textDocument: { uri: 'file://' + mainFile },
		position: { line: 2, character: 4 },
	});
	check('prepare rename gives placeholder', prep && prep.placeholder === 'x');

	const renamed = await request('textDocument/rename', {
		textDocument: { uri: 'file://' + mainFile },
		position: { line: 2, character: 4 },
		newName: 'y',
	});
	check('rename produces edits', renamed && renamed.changes && (renamed.changes['file://' + mainFile] ?? []).length >= 1);

	// workspace symbols
	const ws = await request('workspace/symbol', { query: 'double' });
	check('workspace symbol search finds module export', Array.isArray(ws) && ws.some((s) => s.name === 'double'));

	console.log(failures === 0 ? '\nLSP smoke test passed' : `\n${failures} LSP smoke checks failed`);
	proc.kill();
	fs.rmSync(tmp, { recursive: true, force: true });
	process.exit(failures === 0 ? 0 : 1);
}

main().catch((e) => {
	console.error(e);
	proc.kill();
	process.exit(1);
});
