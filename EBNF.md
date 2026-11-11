# LoxFlux Grammar Reference (EBNF)

## Conventions

Productions use `→`. Terminals appear in quotes. `{ x }` denotes zero or more repetitions of `x`; `[ x ]` denotes an optional `x`. `EOF` marks the end of input.

## 1. Lexical Grammar

```ebnf
letter        → "a" … "z" | "A" … "Z" | "_" ;
digit         → "0" … "9" ;
binDigit      → "0" | "1" ;
hexDigit      → digit | "a" … "f" | "A" … "F" ;

comment       → "//" { anyCharacter − newline }
              | "/*" { anyCharacter − "*/" } "*/" ;            (* not nested *)

keyword       → "and" | "break" | "branch" | "cede" | "class" | "const"
              | "continue" | "do" | "else" | "export" | "false" | "for"
              | "foreach" | "fun" | "if" | "import" | "in" | "instanceof"
              | "lambda" | "nil" | "none" | "or" | "print" | "resume"
              | "return" | "super" | "this" | "throw" | "true" | "typeof"
              | "var" | "while" | "yield" ;

identifier    → letter { letter | digit } ;                    (* keywords match first *)

number        → decimal | binary | hex ;
decimal       → digit { digit } [ "." digit { digit } ]
                [ ("e" | "E") [ "+" | "-" ] digit { digit } ] ;
binary        → "0" ("b" | "B") binDigit { binDigit } ;
hex           → "0" ("x" | "X") hexDigit { hexDigit } ;

string        → '"' { anyCharacter − '"' | escape } '"' ;       (* may span source lines *)
escape        → '\"' | '\\' | '\n' | '\t' ;                     (* any other sequence is kept verbatim *)

moduleLiteral → "@" ( "math" | "array" | "object" | "string"
                    | "sys" | "ctor" | "fiber" | "time" ) ;     (* exact names only *)

operator      → "(" | ")" | "{" | "}" | "[" | "]" | "," | "." | ";" | ":"
              | "+" | "-" | "*" | "/" | "%" | "!" | "!="
              | "=" | "==" | "=>" | "<" | "<=" | ">" | ">="
              | "&" | "|" | "^" | "~" | "<<" | ">>" | ">>>" ;
```

## 2. Phrase Grammar

### 2.1 Program and Declarations

```ebnf
program      → { declaration } EOF ;

declaration  → classDecl | funDecl | varDecl | constDecl | statement ;

classDecl    → "class" identifier [ "<" identifier ] "{" { methodDecl } "}" ;
methodDecl   → identifier functionBody ;              (* the name "init" denotes an initializer *)

funDecl      → "fun" identifier functionBody ;
functionBody → "(" [ parameters ] ")" ( block | "=>" expression ) ;
               (* the "=>" body is valid only after lambda parameters *)
parameters   → identifier { "," identifier } ;

varDecl      → "var" varInit { "," varInit } ";" ;
varInit      → identifier [ "=" expression ] ;

constDecl    → "const" constInit { "," constInit } ";" ;      (* rejected at script top level; module top level is an implicit local scope *)
constInit    → identifier "=" expression ;
```

### 2.2 Statements

```ebnf
statement    → printStmt | ifStmt | branchStmt | returnStmt | whileStmt
            | doWhileStmt | forStmt | foreachStmt | breakStmt | continueStmt
            | block | throwStmt | exportStmt | cedeStmt | yieldStmt
            | exprStmt ;

printStmt    → "print" expression { "," expression } ";" ;
ifStmt       → "if" "(" expression ")" statement [ "else" statement ] ;
branchStmt   → "branch" "{" ( caseClause { caseClause } [ noneClause ] | noneClause ) "}" ;
               (* noneClause, when present, must be last *)
caseClause   → expression ":" statement ;
noneClause   → "none" ":" statement ;
returnStmt   → "return" [ expression ] ";" ;             (* not allowed in top-level script code; an initializer cannot return a value *)
whileStmt    → "while" "(" expression ")" statement ;
doWhileStmt  → "do" statement "while" "(" expression ")" ";" ;
forStmt      → "for" "(" forInit [ expression ] ";" [ expression ] ")" statement ;
forInit      → varDecl | constDecl | expression ";" | ";" ;
foreachStmt  → "foreach" "(" ( "var" | "const" ) identifier "," identifier
                        "in" expression ")" statement ;
breakStmt    → "break" ";" ;                              (* inside loops only *)
continueStmt → "continue" ";" ;                           (* inside loops only *)
throwStmt    → "throw" expression ";" ;
exportStmt   → "export" [ expression ] ";" ;              (* module code only *)
cedeStmt     → "cede" ";" ;
yieldStmt    → "yield" [ expression ] ";" ;               (* not allowed in top-level script code *)
exprStmt     → expression ";" ;
block        → "{" { declaration } "}" ;
```

### 2.3 Expressions (weakest binding first)

```ebnf
expression    → assignment ;

assignment    → target "=" assignment | logicOr ;
target        → identifier
              | postfix "." identifier
              | postfix "[" expression "]" ;      (* any other left-hand side is rejected *)

logicOr       → logicAnd { "or" logicAnd } ;
logicAnd      → bitwise { "and" bitwise } ;
bitwise       → equality { ( "&" | "|" | "^" | "<<" | ">>" | ">>>" ) equality } ;
equality      → relational { ( "==" | "!=" ) relational } ;
relational    → comparison { ( "instanceof" | "in" ) comparison } ;
comparison    → term { ( "<" | ">" | "<=" | ">=" ) term } ;
term          → factor { ( "+" | "-" ) factor } ;
factor        → unary { ( "*" | "/" | "%" ) unary } ;
unary         → ( "!" | "-" | "~" | "typeof" ) unary | postfix ;
postfix       → primary { postfixSuffix } ;
postfixSuffix → "." identifier [ "(" [ arguments ] ")" ]
              | "(" [ arguments ] ")"
              | "[" expression "]" ;

primary       → number | string | identifier
              | "true" | "false" | "nil"
              | "this"                                        (* inside a class body only *)
              | "super" "." identifier [ "(" [ arguments ] ")" ]
                                                             (* inside a class with a superclass only *)
              | "lambda" functionBody
              | "(" expression ")"
              | arrayLiteral
              | objectLiteral
              | moduleLiteral
              | "import" expression
              | "resume" postfix ;                              (* prefix expression; the operand binds at call precedence *)

arrayLiteral  → "[" [ expression { "," expression } ] "]" ;
objectLiteral → "{" [ property { "," property } ] "}" ;
property      → ( identifier | string ) ":" expression ;
arguments     → expression { "," expression } ;
```

### 2.4 Operator Precedence

| Level (weakest → strongest) | Operators | Associativity |
|:---:|:---|:---|
| 1 | `=` assignment | right |
| 2 | `or` | right (parser structure) |
| 3 | `and` | right (parser structure) |
| 4 | `& \| ^ << >> >>>` | left |
| 5 | `== !=` | left |
| 6 | `instanceof` `in` | left |
| 7 | `< > <= >=` | left |
| 8 | `+ -` | left |
| 9 | `* / %` | left |
| 10 | `! - ~ typeof` (prefix) | right |
| 11 | `. () []` (postfix) | chaining |
| 12 | primaries | — |

Differences from C/Java:

- All six bitwise operators (`& | ^ << >> >>>`) share **one** precedence level, weaker than equality. C/Java bind the shift operators tighter than relational/equality and split `&`, `^`, `|` into three levels. Hence `a | b & c` parses as `(a | b) & c`, and `1 << x == y` parses as `1 << (x == y)`.
- `instanceof` and `in` bind **weaker** than the comparison operators (`< > <= >=`); C/Java treat them as one relational level. Hence `a instanceof b < c` parses as `a instanceof (b < c)` (Java: `(a instanceof b) < c`).
- `a & b == c` parses as `a & (b == c)` — the grouping C/Java also produce; listed here because it follows directly from the single bitwise level.

## 3. Builtin Modules

Builtin modules are primaries (`moduleLiteral`) whose members are reached through the ordinary postfix chain. The frozen member name sets are:

| Module | Constants | Functions |
|:---|:---|:---|
| `@math` | `NaN` `Infinity` `EPSILON` `MAX_VALUE` `MIN_VALUE` `MAX_SAFE_INTEGER` `MIN_SAFE_INTEGER` `E` `LN2` `LN10` `LOG2E` `LOG10E` `PI` `SQRT1_2` `SQRT2` | `max` `min` `abs` `floor` `ceil` `round` `pow` `sqrt` `sin` `asin` `cos` `acos` `tan` `atan` `log` `log2` `log10` `exp` `isNaN` `isFinite` `random` `seed` |
| `@array` | — | `resize` `length` `pop` `push` `slice` |
| `@object` | — | `isNumber` `isString` `isStringBuilder` `isFunction` `isClass` `isObject` `isArray` `isTypedArray` `isArrayLike` `isBoolean` `isFiber` `getClass` `getGlobal` `keys` `hasOwn` `freeze` `isFrozen` |
| `@string` | — | `byteLen` `charAt` `utf8Len` `utf8At` `append` `intern` `equals` `slice` `parseInt` `parseFloat` |
| `@time` | — | `nano` `micro` `milli` `second` `utc` |
| `@ctor` | — | `Object` `Array` `F64Array` `F32Array` `U32Array` `I32Array` `U16Array` `I16Array` `U8Array` `I8Array` `StringBuilder` `Fiber` |
| `@fiber` | `PENDING` `SETTLED` `RUNNING` `FAILED` `CANCELLED` | `status` `result` `cancel` `current` |
| `@sys` | — | `gcRun` `allocatedBytes` `pinnedBytes` `log` `error` `input` `readFile` `readBin` `writeFile` `appendFile` `fileExists` |

The global native table (`importNative_global`) currently registers no names.

## 4. Lexical and Syntactic Ambiguities

- `{` — in expression position it introduces an object literal; in statement position it introduces a block.
- `in` — inside a foreach header it is the loop separator; inside an expression it is the relational operator.
- `-` — prefix (unary) or infix (term), resolved by position.
- `=`, `==`, `=>`, `<`, `<=`, `>`, `>=`, `>>`, `>>>` — resolved by maximal munch in the scanner.
- `@` — must be followed by one of the eight exact module names.
