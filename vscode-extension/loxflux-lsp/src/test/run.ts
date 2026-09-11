/*
 * MIT License
 * Copyright (c) 2025 IMSDCrueoft
 * LoxFlux language server - test harness (no VS Code required).
 */

import * as assert from 'assert';
import { lex, TokenType } from '../server/lexer';
import { parse } from '../server/parser';
import { Analyzer, DEFAULT_LINT_SETTINGS } from '../server/analyzer';

let passed = 0;
let failed = 0;

function test(name: string, fn: () => void): void {
	try {
		fn();
		passed++;
		console.log(`  ok - ${name}`);
	} catch (e) {
		failed++;
		console.error(`  FAIL - ${name}`);
		console.error(`    ${e instanceof Error ? e.message : e}`);
	}
}

function analyzeOf(source: string) {
	const lexed = lex(source);
	const script = parse(lexed.tokens);
	script.comments = lexed.comments;
	const analysis = new Analyzer(script, source, DEFAULT_LINT_SETTINGS).analyze();
	return { lexed, script, analysis };
}

// ---- lexer -------------------------------------------------------------

console.log('lexer:');

test('tokenizes identifiers and keywords', () => {
	const { tokens } = lex('var fun class branch instanceOf typeof');
	assert.deepStrictEqual(tokens.slice(0, 6).map((t) => t.type), [
		TokenType.Var, TokenType.Fun, TokenType.Class, TokenType.Branch,
		TokenType.InstanceOf, TokenType.TypeOf,
	]);
});

test('tokenizes numbers', () => {
	const { tokens } = lex('0b1010 0xFF 1.2e+3 123');
	const types = tokens.slice(0, 4).map((t) => t.type);
	assert.deepStrictEqual(types, [
		TokenType.NumberBin, TokenType.NumberHex, TokenType.Number, TokenType.Number,
	]);
});

test('tokenizes strings with escapes', () => {
	const plain = lex('"hello"');
	assert.strictEqual(plain.tokens[0].value, 'hello');
	assert.strictEqual(plain.tokens[0].type, TokenType.String);
	const esc = lex('"a\\"b\\nc"');
	assert.strictEqual(esc.tokens[0].value, 'a"b\nc');
	assert.strictEqual(esc.tokens[0].type, TokenType.StringEscape);
});

test('tokenizes modules', () => {
	const { tokens } = lex('@math @array @object @string @time @ctor @sys @bogus');
	assert.deepStrictEqual(tokens.slice(0, 7).map((t) => t.type), [
		TokenType.ModuleMath, TokenType.ModuleArray, TokenType.ModuleObject,
		TokenType.ModuleString, TokenType.ModuleTime, TokenType.ModuleCtor, TokenType.ModuleSys,
	]);
	assert.strictEqual(tokens[7].type, TokenType.Error);
});

test('tokenizes shift operators', () => {
	const { tokens } = lex('<< >> >>>');
	assert.deepStrictEqual(tokens.slice(0, 3).map((t) => t.text), ['<<', '>>', '>>>']);
	assert.deepStrictEqual(tokens.slice(0, 3).map((t) => t.type), [
		TokenType.BitShl, TokenType.BitSar, TokenType.BitShr,
	]);
});

test('tracks line numbers', () => {
	const { tokens } = lex('a\nb\nc');
	assert.strictEqual(tokens[0].line, 0);
	assert.strictEqual(tokens[1].line, 1);
	assert.strictEqual(tokens[2].line, 2);
});

// ---- parser ------------------------------------------------------------

console.log('parser:');

test('parses var declarations with multiple declarators', () => {
	const { script } = analyzeOf('var a = 1, b = 2, c;');
	assert.strictEqual(script.errors.length, 0);
	const decl = script.statements[0];
	assert.strictEqual(decl.kind, 'VarDecl');
	if (decl.kind === 'VarDecl') {
		assert.strictEqual(decl.declarators.length, 3);
	}
});

test('parses class with inheritance and methods', () => {
	const { script } = analyzeOf('class A < B { init(x) { this.x = x; } speak() { print "hi"; } }');
	assert.strictEqual(script.errors.length, 0);
	assert.strictEqual(script.statements[0].kind, 'ClassDecl');
});

test('parses branch statement', () => {
	const { script } = analyzeOf('branch { a == 1: print "one"; none: print "other"; }');
	assert.strictEqual(script.errors.length, 0);
	assert.strictEqual(script.statements[0].kind, 'Branch');
});

test('parses lambda forms', () => {
	const block = analyzeOf('var f = lambda (a, b) { return a + b; };');
	assert.strictEqual(block.script.errors.length, 0);
	const arrow = analyzeOf('var g = lambda (a) => a * 2;');
	assert.strictEqual(arrow.script.errors.length, 0);
});

test('parses do-while and for loops', () => {
	const dw = analyzeOf('do { print 1; } while (a < 3);');
	assert.strictEqual(dw.script.errors.length, 0);
	const fr = analyzeOf('for (var i = 0; i < 10; i = i + 1) { print i; }');
	assert.strictEqual(fr.script.errors.length, 0);
});

test('parses import expression and export statement', () => {
	const imp = analyzeOf('var thing = import "./module.lfx";');
	assert.strictEqual(imp.script.errors.length, 0);
	const exp = analyzeOf('export { "pi": PI, "double": lambda (a) => a * 2 };');
	assert.strictEqual(exp.script.errors.length, 0);
});

test('parses bitwise operations', () => {
	const { script } = analyzeOf('var x = 0b1010 & 0b1100 | 0x1 ^ 5 << 2 >> 1 >>> 3;');
	assert.strictEqual(script.errors.length, 0);
});

test('reports errors with recovery', () => {
	const { script } = analyzeOf('var a = ;\nprint 1;\nvar b = 2;');
	assert.ok(script.errors.length > 0);
	assert.ok(script.statements.length >= 2, 'should recover and parse remaining statements');
});

// ---- analyzer ----------------------------------------------------------

console.log('analyzer:');

test('resolves locals and globals', () => {
	const { analysis } = analyzeOf('var g = 1;\nfun f(a) { var b = a + g; return b; }');
	assert.strictEqual(analysis.diagnostics.length, 0);
	assert.strictEqual(analysis.symbols.length, 4); // g, f, a, b
});

test('flags undefined variables', () => {
	const { analysis } = analyzeOf('print undefinedThing;');
	const undeclared = analysis.diagnostics.filter((d) => d.code === 'undeclared');
	assert.strictEqual(undeclared.length, 1);
});

test('flags assignment to const', () => {
	const { analysis } = analyzeOf('const K = 1;\nK = 2;');
	const errors = analysis.diagnostics.filter((d) => d.code === 'assign-to-const');
	assert.strictEqual(errors.length, 1);
});

test('flags top-level const like the real compiler', () => {
	const { analysis } = analyzeOf('const K = 5;');
	const errors = analysis.diagnostics.filter((d) => d.code === 'const-scope');
	assert.strictEqual(errors.length, 1);
	// inside a block it is fine
	const ok = analyzeOf('{ const K = 5; print K; }');
	assert.strictEqual(ok.analysis.diagnostics.filter((d) => d.code === 'const-scope').length, 0);
});

test('resolves class members and this.fields', () => {
	const source = 'class P {\n init() { this.name = "x"; }\n get() { return this.name; }\n}';
	const { analysis } = analyzeOf(source);
	assert.strictEqual(analysis.diagnostics.length, 0);
	const cls = analysis.classes.get('P');
	assert.ok(cls);
	assert.ok(cls!.fields.has('name'));
	assert.ok(cls!.methods.has('init'));
	assert.ok(cls!.methods.has('get'));
});

test('extracts exported names', () => {
	const source = 'const PI = 3.14;\nfun double(a) { return a * 2; }\nexport { "pi": PI, "double": double };';
	const { analysis } = analyzeOf(source);
	assert.deepStrictEqual(analysis.exports.map((e) => e.name), ['pi', 'double']);
});

test('collects import info', () => {
	const { analysis } = analyzeOf('var m = import "./lib.lfx";');
	assert.strictEqual(analysis.imports.length, 1);
	assert.strictEqual(analysis.imports[0].path, './lib.lfx');
});

test('attaches doc comments', () => {
	const source = '// Adds two numbers.\nfun add(a, b) { return a + b; }';
	const { analysis } = analyzeOf(source);
	const add = analysis.globals.find((s) => s.name === 'add');
	assert.strictEqual(add?.doc, 'Adds two numbers.');
});

test('detects unreachable code', () => {
	const { analysis } = analyzeOf('fun f() { return 1; print 2; }');
	const unreachable = analysis.diagnostics.filter((d) => d.code === 'unreachable');
	assert.strictEqual(unreachable.length, 1);
});

console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed > 0 ? 1 : 0);
