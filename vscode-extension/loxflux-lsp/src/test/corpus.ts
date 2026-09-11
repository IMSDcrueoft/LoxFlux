/*
 * Corpus check: parse every .lfx file shipped with LoxFlux and report
 * diagnostics counts. Some scripts intentionally contain errors
 * (e.g. funcError.lfx), so we print details instead of asserting zero.
 */
import * as fs from 'fs';
import * as path from 'path';
import { lex } from '../server/lexer';
import { parse } from '../server/parser';
import { Analyzer, DEFAULT_LINT_SETTINGS } from '../server/analyzer';

const scriptsDir = path.join(__dirname, '..', '..', '..', '..', 'LoxFlux-master', 'scripts');

const files = fs.readdirSync(scriptsDir).filter((f) => f.endsWith('.lfx'));
let totalErrors = 0;

for (const file of files.sort()) {
	const text = fs.readFileSync(path.join(scriptsDir, file), 'utf8');
	const lexed = lex(text);
	const script = parse(lexed.tokens);
	script.comments = lexed.comments;
	const analysis = new Analyzer(script, text, { ...DEFAULT_LINT_SETTINGS, undeclaredVariable: 'off' }).analyze();

	const errors = analysis.diagnostics.filter((d) => d.severity === 'error');
	totalErrors += errors.length;
	const status = errors.length === 0 ? 'clean' : `${errors.length} error(s)`;
	console.log(`${file.padEnd(36)} ${status}`);
	for (const e of errors.slice(0, 4)) {
		const line = text.slice(0, e.start).split('\n').length;
		console.log(`    line ${line}: ${e.message}`);
	}
}

console.log(`\n${files.length} files parsed, ${totalErrors} total error diagnostics`);
