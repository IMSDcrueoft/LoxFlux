/*
 * MIT License
 * Copyright (c) 2025 IMSDCrueoft
 * LoxFlux language server - builtin modules, keywords and native globals.
 * Docs derived from the LoxFlux README.
 */

export interface BuiltinMember {
	name: string;
	kind: 'function' | 'class';
	signature: string;
	doc: string;
}

export interface BuiltinModule {
	name: string;
	doc: string;
	members: BuiltinMember[];
}

function fn(name: string, params: string, doc: string): BuiltinMember {
	return { name, kind: 'function', signature: `${name}(${params})`, doc };
}

function cls(name: string, params: string, doc: string): BuiltinMember {
	return { name, kind: 'class', signature: `${name}(${params})`, doc };
}

export const BUILTIN_MODULES: Record<string, BuiltinModule> = {
	'@math': {
		name: '@math',
		doc: 'Mathematical functions and utilities implemented as native bindings.',
		members: [
			fn('max', 'a, b, ...', 'Returns the maximum value among the provided arguments.'),
			fn('min', 'a, b, ...', 'Returns the minimum value among the provided arguments.'),
			fn('abs', 'x', 'Computes the absolute value of a number.'),
			fn('floor', 'x', 'Rounds a number down to the nearest integer.'),
			fn('ceil', 'x', 'Rounds a number up to the nearest integer.'),
			fn('round', 'x', 'Rounds a number to the nearest integer.'),
			fn('pow', 'x, y', 'Computes the power of a number (x^y).'),
			fn('sqrt', 'x', 'Computes the square root of a number.'),
			fn('exp', 'x', 'Computes the exponential function (e^x).'),
			fn('log', 'x', 'Computes the natural logarithm (ln) of a number.'),
			fn('log2', 'x', 'Computes the base-2 logarithm of a number.'),
			fn('log10', 'x', 'Computes the base-10 logarithm of a number.'),
			fn('sin', 'x', 'Computes the sine of an angle (in radians).'),
			fn('cos', 'x', 'Computes the cosine of an angle (in radians).'),
			fn('tan', 'x', 'Computes the tangent of an angle (in radians).'),
			fn('asin', 'x', 'Computes the arcsine (inverse sine) of a number.'),
			fn('acos', 'x', 'Computes the arccosine (inverse cosine) of a number.'),
			fn('atan', 'x', 'Computes the arctangent (inverse tangent) of a number.'),
			fn('random', '', 'Generates a pseudo-random number using the xoshiro256** algorithm.'),
			fn('seed', 'n', 'Initializes the random number generator with a specific seed value.'),
			fn('isNaN', 'v', 'Checks if a value is NaN (Not a Number).'),
			fn('isFinite', 'v', 'Checks if a value is finite (not infinite or NaN).'),
		],
	},
	'@array': {
		name: '@array',
		doc: 'Utilities for creating and manipulating arrays.',
		members: [
			fn('resize', 'arr, len', 'Resizes an existing array to a new length. New slots are zero-initialized; excess elements are discarded.'),
			fn('length', 'arr', 'Returns the current number of elements in the array.'),
			fn('pop', 'arr', 'Removes and returns the last element of the array. Returns nil when out of range.'),
			fn('push', 'arr, item, ...', 'Appends one or more elements to the end of the array (in place) and returns the new length.'),
			fn('slice', 'arr, start, end?', 'Extracts a section of an array and returns it as a new array. Supports negative indices.'),
		],
	},
	'@object': {
		name: '@object',
		doc: 'Type checking and object introspection utilities.',
		members: [
			fn('isClass', 'v', 'Verifies if a value is a class.'),
			fn('isFunction', 'v', 'Verifies if a value is a function or a native function.'),
			fn('isObject', 'v', 'Verifies if a value is an object.'),
			fn('isArray', 'v', 'Verifies if a value is an array.'),
			fn('isArrayLike', 'v', 'Verifies if a value is an array or a typed array.'),
			fn('isTypedArray', 'v', 'Verifies if a value is a typed array.'),
			fn('isString', 'v', 'Verifies if a value is a string.'),
			fn('isStringBuilder', 'v', 'Verifies if a value is a StringBuilder.'),
			fn('isNumber', 'v', 'Verifies whether a value is a number.'),
			fn('isBoolean', 'v', 'Verifies whether a value is a boolean.'),
			fn('getGlobal', '', 'Returns the global object.'),
			fn('keys', 'obj', 'Returns the own keys array of an instance.'),
		],
	},
	'@string': {
		name: '@string',
		doc: 'String manipulation and high-performance string building (ASCII + UTF-8).',
		members: [
			fn('length', 's', 'Returns the byte length of a string.'),
			fn('utf8Len', 's', 'Returns the character count for UTF-8 strings (ignoring byte-level details). e.g. `@string.utf8Len("αβγ")` → `3`'),
			fn('charAt', 's, i', 'Retrieves an ASCII character by byte position.'),
			fn('utf8At', 's, i', 'Retrieves a UTF-8 character by logical character position. e.g. `@string.utf8At("αβγ", 1)` → `"β"`'),
			fn('append', 'builder, value', 'Efficiently appends a string or stringBuilder to a StringBuilder (numbers must be converted by the caller).'),
			fn('intern', 'builder', 'Converts a StringBuilder to an immutable string, or returns existing strings directly.'),
			fn('equals', 'a, b', 'Compares whether the content of two strings/stringBuilders is the same.'),
			fn('slice', 's, start, end?', 'Extracts a section of a string or StringBuilder and returns a new StringBuilder. Supports negative indices.'),
			fn('parseInt', 's, base?', 'Parses a string to an integer (supports hex/octal/binary prefixes, base 2 to 36).'),
			fn('parseFloat', 's', 'Parses a string to a float (supports scientific notation).'),
		],
	},
	'@time': {
		name: '@time',
		doc: 'Precise timing functions implemented as native bindings.',
		members: [
			fn('nano', '', 'Returns the current time in nanoseconds (1e-9 s) since an arbitrary reference point.'),
			fn('micro', '', 'Returns the current time in microseconds (1e-6 s) since an arbitrary reference point.'),
			fn('milli', '', 'Returns the current time in milliseconds (1e-3 s) since an arbitrary reference point.'),
			fn('second', '', 'Returns the current time in seconds since an arbitrary reference point.'),
			fn('utc', '', 'Returns the current UTC time in milliseconds since the Unix epoch.'),
		],
	},
	'@ctor': {
		name: '@ctor',
		doc: 'Built-in type constructors.',
		members: [
			cls('Object', '', 'Creates an empty object that can hold string-value pairs of any supported type.'),
			cls('Array', '', 'Creates a generic dynamic array that can hold elements of any supported type.'),
			cls('F64Array', 'len', 'Creates a fixed-size array of 64-bit floating-point numbers (IEEE 754 double precision).'),
			cls('F32Array', 'len', 'Creates a fixed-size array of 32-bit floating-point numbers (IEEE 754 single precision).'),
			cls('U32Array', 'len', 'Creates a fixed-size array of 32-bit unsigned integers.'),
			cls('I32Array', 'len', 'Creates a fixed-size array of 32-bit signed integers.'),
			cls('U16Array', 'len', 'Creates a fixed-size array of 16-bit unsigned integers.'),
			cls('I16Array', 'len', 'Creates a fixed-size array of 16-bit signed integers.'),
			cls('U8Array', 'len', 'Creates a fixed-size array of 8-bit unsigned integers (commonly used for byte-level operations).'),
			cls('I8Array', 'len', 'Creates a fixed-size array of 8-bit signed integers.'),
			cls('StringBuilder', 'init?', 'Creates a mutable string buffer, optionally initialized with a string or another builder.'),
		],
	},
	'@sys': {
		name: '@sys',
		doc: 'Low-level system utilities: IO, garbage collection and memory statistics.',
		members: [
			fn('log', 'v, ...', 'Prints values to stdout. Unlike the `print` keyword it allows multiple inputs and expands array contents (non-recursively).'),
			fn('error', 'v', 'Outputs string or stringBuilder information to stderr.'),
			fn('input', '', 'Reads a line of input from the console and returns a StringBuilder.'),
			fn('readFile', 'path', 'Reads the contents of a file and returns them as a StringBuilder object.'),
			fn('gc', '', 'Triggers a full garbage collection cycle.'),
			fn('gcNext', 'bytes', 'Configures the heap memory usage to be used for the next GC trigger.'),
			fn('gcBegin', 'bytes', 'Configures the limits of the initial GC.'),
			fn('allocated', '', 'Returns the total number of bytes currently allocated in the dynamic memory pool.'),
			fn('static', '', 'Returns the total number of bytes allocated for static objects (e.g. strings, functions).'),
		],
	},
};

export interface KeywordDoc {
	keyword: string;
	doc: string;
	/** optional snippet body (with placeholders) */
	snippet?: string;
}

/** Statement/keyword completions. */
export const KEYWORD_DOCS: KeywordDoc[] = [
	{ keyword: 'print', doc: 'Prints a value to stdout followed by a newline.', snippet: 'print ${1:value};' },
	{ keyword: 'var', doc: 'Declares one or more variables.\n\n```\nvar a = 1, b = 2, c = a + b;\n```', snippet: 'var ${1:name} = ${2:value};' },
	{ keyword: 'const', doc: "Declares one or more constants that must be initialized.\n\n```\nconst PI = 3.14159;\n```", snippet: 'const ${1:NAME} = ${2:value};' },
	{ keyword: 'fun', doc: 'Declares a function.\n\n```\nfun add(a, b) {\n\treturn a + b;\n}\n```', snippet: 'fun ${1:name}(${2:params}) {\n\t${3}\n}' },
	{ keyword: 'class', doc: 'Declares a class with optional inheritance.\n\n```\nclass Animal < Life {\n\tinit(name) {\n\t\tthis.name = name;\n\t}\n\tspeak() {\n\t\tprint "...";\n\t}\n}\n```', snippet: 'class ${1:Name} {\n\t${2}\n}' },
	{ keyword: 'if', doc: 'Conditional statement: `if (cond) { ... } else { ... }`', snippet: 'if (${1:condition}) {\n\t${2}\n}' },
	{ keyword: 'else', doc: 'Else branch of an `if` statement.' },
	{ keyword: 'branch', doc: 'Simplifies `if-else if` chains (alternative to switch-case). The `none` case (like default) must appear last.\n\n```\nbranch {\n\tvalue == 1: print "one";\n\tnone: print "other";\n}\n```', snippet: 'branch {\n\t${1:condition}: ${2};\n\tnone: ${3};\n}' },
	{ keyword: 'none', doc: 'Default case of a `branch` block. Must be the last case.' },
	{ keyword: 'while', doc: 'While loop: `while (cond) { ... }`', snippet: 'while (${1:condition}) {\n\t${2}\n}' },
	{ keyword: 'do', doc: 'Do-while loop: the body runs at least once.\n\n```\ndo { ... } while (cond);\n```', snippet: 'do {\n\t${1}\n} while (${2:condition});' },
	{ keyword: 'for', doc: 'For loop: `for (init; cond; inc) { ... }`', snippet: 'for (var ${1:i} = 0; ${1:i} < ${2:n}; ${1:i} = ${1:i} + 1) {\n\t${3}\n}' },
	{ keyword: 'break', doc: 'Exits the enclosing loop.' },
	{ keyword: 'continue', doc: 'Jumps to the next iteration of the enclosing loop.' },
	{ keyword: 'return', doc: 'Returns a value from the enclosing function.', snippet: 'return ${1:value};' },
	{ keyword: 'throw', doc: 'Throws an error value.', snippet: 'throw ${1:value};' },
	{ keyword: 'lambda', doc: 'Declares an anonymous function inline. Supports block form `lambda (a, b) { ... }` and arrow form `lambda (a) => a * 2`.', snippet: 'lambda (${1:params}) => ${2:expr}' },
	{ keyword: 'import', doc: 'Loads, compiles and executes a script file, returning whatever the module exports.\n\n```\nvar thing = import "./module.lfx";\n```', snippet: 'import "./${1:module}.lfx";' },
	{ keyword: 'export', doc: 'Used within a module file to specify what value is returned to the importing file.\n\n```\nexport { "pi": PI };\n```', snippet: 'export ${1:value};' },
	{ keyword: 'this', doc: 'Refers to the current instance inside a method.' },
	{ keyword: 'super', doc: 'Calls a method from the superclass: `super.method(args)`.' },
	{ keyword: 'typeof', doc: "Returns a string describing the item's subdivision type." },
	{ keyword: 'instanceof', doc: 'Checks if an object is an instance of a specific class: `x instanceof Foo`.' },
	{ keyword: 'and', doc: 'Logical AND (short-circuit).' },
	{ keyword: 'or', doc: 'Logical OR (short-circuit).' },
	{ keyword: 'nil', doc: 'The nil (null) literal.' },
	{ keyword: 'true', doc: 'The boolean true literal.' },
	{ keyword: 'false', doc: 'The boolean false literal.' },
];

/** Module token text -> module key used in BUILTIN_MODULES. */
export function moduleKeyForToken(text: string): string | undefined {
	if (text.startsWith('@') && text in BUILTIN_MODULES) {
		return text;
	}
	return undefined;
}

/** Literal token types of `@...` modules, used by the lexer/parser. */
export const MODULE_TOKEN_NAMES: string[] = Object.keys(BUILTIN_MODULES);
