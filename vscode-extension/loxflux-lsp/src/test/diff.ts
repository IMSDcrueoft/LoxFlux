/*
 * Differential-testing helper: analyze one .lfx file with the LSP pipeline
 * and print a classification line:  ok | error:<count> | warn:<count>
 */
import * as fs from 'fs';
import { lex } from '../server/lexer';
import { parse } from '../server/parser';
import { Analyzer, DEFAULT_LINT_SETTINGS } from '../server/analyzer';

const file = process.argv[2];
const text = fs.readFileSync(file, 'utf8');
const lexed = lex(text);
const script = parse(lexed.tokens);
script.comments = lexed.comments;
const analysis = new Analyzer(script, text, DEFAULT_LINT_SETTINGS).analyze();

const errors = analysis.diagnostics.filter((d) => d.severity === 'error');
const warnings = analysis.diagnostics.filter((d) => d.severity === 'warning');

if (errors.length > 0) {
	console.log(`error:${errors.length}`);
} else if (warnings.length > 0) {
	console.log(`warn:${warnings.length}`);
} else {
	console.log('ok');
}
