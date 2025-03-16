# LoxFlux(a clox based interpreter) Project Introduction

## Introduction

Lox is a programming language designed for learning purposes. It is conceived as a small and easy-to-understand language, perfect for those who want to build their own interpreter or compiler. The Lox language and its associated tutorials were originally introduced by Bob Nystrom in his online book "Crafting Interpreters". ([Book URL](https://craftinginterpreters.com/))

Lox 是一种专为学习设计的编程语言。它被设计成一门精简且易于理解的语言，非常适合那些希望构建自己的解释器或编译器的人士使用。Lox 语言及其相关教程最初由 Bob Nystrom 在他的在线书籍《Crafting Interpreters》中介绍。([书籍网址](https://craftinginterpreters.com/))

## Resources

- [Crafting Interpreters - Official Website](https://craftinginterpreters.com/)
- [Crafting Interpreters - Official Github](https://github.com/munificent/craftinginterpreters)
  
## Project Status
This project "LoxFlux" is being developed based on the cLox version (A bytecode-virtual-machine based on stack) for learning and development purposes. 
Please note that this project has not yet completed its basic functionalities, thus it should not be used for practical applications.

此项目“LoxFlux”是基于 cLox 版本(栈型字节码虚拟机)进行学习和开发的。请注意，该项目目前尚未完成基础功能，因此请勿将其用于实际应用中。

## Features

### Numbers

- **Binary literal**: Use `0b` or `0B` prefix, e.g., `0b1010`.
- **Hexadecimal literal**: Use `0x` or `0X` prefix, e.g., `0xFF`.
- **Scientific notation**: Supports formats like `1.2e+3` and `123E-2`.

### String

- **Escape characters**: Supports escaping with backslash `\`, such as `\"` for double quotes; Example: `"\"hello world\""` renders as `"hello world"`.

### Constants

- **Constant range**: Expands to `0x00ffffff` (16,777,215).
- **Constant deduplication**: For numbers and strings.

### Global Variable

- **Optimized global variable access**: Achieves O(1) time complexity.

### Local Variable

- **Local variable range**: Expands to support up to 1024 variables.
- **'const' keyword support**: Supported within blocks.

### Loop

- **'break' and 'continue' keywords**: Supported within loops.

### Random Generator

- **WellRng1024a implementation**: Provided for random number generation.

### Timer

- **Cross-platform interface**: Includes `get_nanoseconds()` and `get_utc_milliseconds()`.

### Comment

- **Block comment support**: Using `/* */`.

### REPL

- **Support for line break input**: Use `\` for multi-line input in REPL.
- **Commands**:
  - `/help` : Print help information.
  - `/exit` : Exit the REPL.
  - `/clear`: Clean the console.
  - `/eval` : Load file and Run.
  - `/mem`  : Print memory information (using mimalloc).

## Licenses
The project is based on the [MIT license] and uses two third-party projects.LLVM may be used as a JIT in the future.

1. **mimalloc**
   - Copyright (c) 2018-2025 Microsoft Corporation, Daan Leijen
   - License: MIT

2. **xxHash**
   - Copyright (c) 2012-2021 Yann Collet
   - License: BSD 2-Clause
