# LoxFlux

LoxFlux is being developed based on the cLox version of Lox language (A bytecode-virtual-machine based on stack) for learning and development purposes. 
Please note that this project has not yet completed its basic functionalities, thus it should not be used for practical applications.

此项目“LoxFlux”是基于Lox语言的cLox 版本(栈型字节码虚拟机)进行学习和开发的。请注意，该项目目前尚未完成基础功能，因此请勿将其用于实际应用中。

## Introduction of Lox

Lox is a programming language designed for learning purposes. It is conceived as a small and easy-to-understand language, perfect for those who want to build their own interpreter or compiler. The Lox language and its associated tutorials were originally introduced by Bob Nystrom in his online book "Crafting Interpreters". ([Book URL](https://craftinginterpreters.com/))

## Resources of Lox

- [Crafting Interpreters - Official Website](https://craftinginterpreters.com/)
- [Crafting Interpreters - Official Github](https://github.com/munificent/craftinginterpreters)

## Extra Features

### Numbers

- **Binary literal**: Use `0b` or `0B` prefix, e.g., `0b1010`.
- **Hexadecimal literal**: Use `0x` or `0X` prefix, e.g., `0xFF`.
- **Scientific notation**: Supports formats like `1.2e+3` and `123E-2`.

---

### String

- **Escape characters**: Supports escaping with backslash `\`, such as `\"` for double quotes; Example: `"\"hello world\""` renders as `"hello world"`.

---

### Constants

- **Constant range**: Expands to `0x00ffffff` (16,777,215).
- **Constant deduplication**: For numbers and strings.

---

### Global Variable

- **Optimized global variable access**: Achieves O(1) time complexity of existing keys indexes that do not deteriorate.
- **Global instance**: Use @global to explicitly get the global table, create and delete attributes, use it like a simple object.

---

### Local Variable

- **Local variable range**: Expands to support up to 1024 variables(Configurable up to 65534).
- **'const' keyword support**: Supported within blocks.

---

### Lambda
- **Lambda Syntax**: An anonymous function can be declared inline using the `lambda` keyword. This provides a concise way to define functions without explicitly naming them, making it suitable for short, inline operations.
- **Syntax**: The syntax for a lambda expression is `lambda (parameters) { body }`, where `parameters` are the input arguments and `body` contains the logic to be executed.
- **Use Cases**: Lambda expressions are commonly used in functional programming patterns, such as passing functions as arguments to higher-order functions (e.g., `map`, `filter`, `reduce`).

---

### Loop

- **'break' and 'continue' keywords**: Supported within loops.

---

### Instance

- **Delete property**: Remove key-value pairs by assigning nil to the object.

---

### Timer

- **Cross-platform interface**: Includes `get_nanoseconds()` and `get_utc_milliseconds()`.

---

### Comment

- **Block comment support**: Using `/* */`.

---

### GC

- **Detached static objects and dynamic objects**: Static objects such as strings/functions, they don't usually bloat very much, so I think it's a viable option not to recycle them.

---

### Built-in Modules

There are some namespace objects that start with '@' available, and since they are not in the global scope, the initial state of the global scope is a "completely clean" state. 

#### @math @array @object @string @time @file @system

The `@math` module provides a comprehensive set of mathematical functions and utilities, implemented as native bindings for efficiency and ease of use. These functions are accessible globally and can be used directly in scripts or applications.

- **Basic Arithmetic Functions**:
  - `max`: Returns the maximum value among the provided arguments.
  - `min`: Returns the minimum value among the provided arguments.
  - `abs`: Computes the absolute value of a number.
  - `floor`: Rounds a number down to the nearest integer.
  - `ceil`: Rounds a number up to the nearest integer.
  - `round`: Rounds a number to the nearest integer.

- **Exponential and Logarithmic Functions**:
  - `pow`: Computes the power of a number (x^y).
  - `sqrt`: Computes the square root of a number.
  - `exp`: Computes the exponential function (e^x).
  - `log`: Computes the natural logarithm (ln) of a number.
  - `log2`: Computes the base-2 logarithm of a number.
  - `log10`: Computes the base-10 logarithm of a number.

- **Trigonometric Functions**:
  - `sin`: Computes the sine of an angle (in radians).
  - `cos`: Computes the cosine of an angle (in radians).
  - `tan`: Computes the tangent of an angle (in radians).
  - `asin`: Computes the arcsine (inverse sine) of a number.
  - `acos`: Computes the arccosine (inverse cosine) of a number.
  - `atan`: Computes the arctangent (inverse tangent) of a number.

- **Random Number Generation**:
  - `random`: Generates a pseudo-random number using the WellRng1024a algorithm.
  - `seed`: Initializes the random number generator with a specific seed value.

- **Special Value Checks**:
  - `isNaN`: Checks if a value is NaN (Not a Number).
  - `isFinite`: Checks if a value is finite (not infinite or NaN).

These functions are designed to provide robust mathematical capabilities while maintaining high performance through native implementation. They are ideal for scientific computations, game development, simulations, and other domains requiring precise numerical operations.

---

The `@object` module provides utilities for type checking and object introspection. These functions are essential for determining the nature of values and ensuring type safety in dynamic environments.

- **Type Checking**:
  - `instanceOf`: Checks if an object is an instance of a specific class.
  - `isClass`: Determines whether a value is a class.
  - `isObject`: Verifies if a value is an object.
  - `isString`: Checks if a value is a string.
  - `isNumber`: Determines whether a value is a number.

These functions are particularly useful for runtime type validation and debugging, allowing developers to write robust and error-resistant code.

---

The `@system` module offers low-level system utilities, primarily focused on memory management and garbage collection. These functions provide insights into the runtime environment and allow fine-grained control over resource allocation.

- **Garbage Collection**:
  - `gc`: Triggers a full garbage collection cycle.
  - `gcNext`: Configure the heap memory usage to be used for the next GC trigger.
  - `gcBegin`: Configure the limits of the initial GC.

- **Memory Statistics**:
  - `allocated`: Returns the total number of bytes currently allocated in the dynamic memory pool.
  - `allocatedStatic`: Returns the total number of bytes allocated for static objects(e.g., strings, functions).

These utilities are invaluable for monitoring and optimizing memory usage, especially in long-running applications or environments with limited resources. They enable developers to manage memory explicitly and diagnose potential memory leaks or inefficiencies.

---

### REPL

- **Support for line break input**: Use `\` for multi-line input in REPL.
- **Commands**:
  - `/help` : Print help information.
  - `/exit` : Exit the REPL.
  - `/clear`: Clean the console.
  - `/eval` : Load file and Run.
  - `/mem`  : Print memory information (using mimalloc).

## Licenses
The project is based on the [MIT license] and uses two third-party projects.LLVM/CraneLift may be used as a JIT in the future.

1. **mimalloc**
   - Copyright (c) 2018-2025 Microsoft Corporation, Daan Leijen
   - License: MIT

2. **xxHash**
   - Copyright (c) 2012-2021 Yann Collet
   - License: BSD 2-Clause
