# Change Log

All notable changes to the LoxFlux Language Server extension will be documented in this file.

## [0.1.2]

- Top-level `const` now reports a warning instead of an error, since module files (imported) legally run in local scope where `const` is allowed.

## [0.1.1]

- Fix semantic tokens: remove double delta-encoding that corrupted token positions (e.g. `@math` rendered as `ma`, only first occurrence highlighted).

## [0.1.0]

- Initial release.
- Diagnostics: syntax errors, undefined variables, `const` assignment, unused locals, unreachable code, unresolved imports.
- Hover with doc comments, builtin module docs and keyword documentation.
- Completion: scope-aware symbols, `@module` members, member completion (classes/modules/imports), keyword snippets.
- Go to definition (including cross-file via `import`), find references, rename.
- Document & workspace symbols.
- Semantic tokens.
