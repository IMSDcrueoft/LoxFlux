# LoxFlux Language Server (VS Code)

Full language support for the [LoxFlux](https://github.com/IMSDcrueoft/LoxFlux) interpreter (`.lfx` / `.lox` files) built on the Language Server Protocol.

The language server is implemented in TypeScript and ships with its own LoxFlux lexer/parser ported from the C sources (`src/scanner.c`, `src/compiler.c`) — no interpreter binary is required.

## Features

| Feature | Notes |
| --- | --- |
| Diagnostics | Syntax errors (lexer + parser), undefined variables, assignment to `const`, unused locals, unreachable code, unresolved imports |
| Hover | Symbol signatures with attached doc comments, builtin `@module` member docs, keyword documentation, import path resolution |
| Completion | Scope-aware (locals → globals), builtin modules (`@math`, `@array`, `@object`, `@string`, `@time`, `@ctor`, `@sys`) with full member signatures, member completion after `.` (class methods/fields, module members, imported exports), keywords with snippets |
| Go to Definition | Locals, globals, classes, methods, class fields; imported symbols jump into the module file; import path strings open the module |
| Find References | All usages of a symbol inside its document (includes declaration) |
| Rename | Symbolic renames (variables, functions, classes, methods, parameters) across the document |
| Document Symbols | Outline of classes (with methods), functions and variables |
| Workspace Symbols | Searches indexed `.lfx`/`.lox` files in the workspace |
| Semantic Tokens | Variables, parameters, functions, methods, classes, properties, namespaces — layered on top of the [syntax highlighting extension](../loxflux-syntax-highlight/) |

### Settings

| Setting | Default | Description |
| --- | --- | --- |
| `loxflux.trace.server` | `off` | Trace communication between VS Code and the server |
| `loxflux.lint.undeclaredVariable` | `warning` | Severity of reads of never-declared variables (`error` / `warning` / `off`) |
| `loxflux.lint.unusedVariable` | `false` | Warn about declared-but-never-read locals |
| `loxflux.lint.assignToConst` | `true` | Report assignments to `const` variables |
| `loxflux.lint.unreachableCode` | `true` | Report statements after `return`/`throw`/`break`/`continue` |

> Tip: install the **LoxFlux Syntax Highlight** extension alongside this one for TextMate-grade coloring (strings, numbers, comments); this extension contributes the language definition, language configuration and semantic tokens, and works standalone as well.

## Development

```bash
npm install        # install dependencies
npm run compile    # bundle client + server with esbuild
npm run watch      # rebuild on change
npm run typecheck  # tsc --noEmit
npm test           # unit tests for lexer/parser/analyzer
npm run smoke      # end-to-end LSP protocol test (spawns out/server.js)
npm run package    # build a .vsix (requires @vscode/vsce)
```

### Debugging in VS Code

1. `npm run watch`
2. Open this folder in VS Code and press `F5` (Extension Development Host).
3. Open any `.lfx`/`.lox` file.

The server runs with `--inspect=6019` in debug mode; attach your debugger to that port.

## Project layout

```
src/
├── client/extension.ts     VS Code client (LanguageClient setup)
├── server/
│   ├── server.ts           LSP server entry (capabilities + handlers)
│   ├── lexer.ts            LoxFlux tokenizer (port of src/scanner.c)
│   ├── parser.ts           Recursive-descent parser (port of src/compiler.c)
│   ├── ast.ts              AST node definitions
│   ├── analyzer.ts         Scopes, symbol resolution, lints
│   ├── infer.ts            Lightweight receiver type inference
│   ├── documents.ts        Document store + import resolution
│   ├── positions.ts        Offset <-> LSP position conversion
│   ├── walker.ts           Shared AST traversal
│   ├── builtins.ts         Builtin modules/keywords data (from README)
│   └── features/           hover, completion, navigation, symbols, semanticTokens
└── test/                   unit tests + LSP protocol smoke test
```

## Limitations

- Find References / Rename operate within the current document (cross-file usages through `import` are resolved for definition/hover but not renumbered on rename).
- Type inference is heuristic-based (constructor calls, imports, `this`); dynamic Lox programs can always defeat it.

## License

MIT — see the LoxFlux repository for details.
