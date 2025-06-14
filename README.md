# LoxFlux
![Version](https://img.shields.io/badge/version-0.9.9-blue)
[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/IMSDcrueoft/LoxFlux)

LoxFlux is an independent reimplementation of the cLox interpreter described in "Crafting Interpreters"—a stack-based bytecode virtual machine. All code is written from scratch, following the book’s design principles. This project is still actively being developed and improved.

- **Design metrics**: loxFlux should be fully compatible with the lox syntax, so lox scripts can be painlessly interpreted and executed in loxFlux (although error handling may vary, the "correct code" is the same).

## Introduction of Lox

Lox is a programming language designed for learning purposes. It is conceived as a small and easy-to-understand language, perfect for those who want to build their own interpreter or compiler. The Lox language and its associated tutorials were originally introduced by Bob Nystrom in his online book "Crafting Interpreters". ([Book URL](https://craftinginterpreters.com/))

## Resources of Lox

- [Crafting Interpreters - Official Website](https://craftinginterpreters.com/)
- [Crafting Interpreters - Official Github](https://github.com/munificent/craftinginterpreters)

## List of LoxFlux features

#### Performance

- **Shared constants**: Use a shared constant table instead of a function holding its own constant table individually.
- **Constant range**: Expands to `0x00ffffff` (16,777,215)(will reduce perf).
- **Local variable range**: Expands to support up to 1023 nested variables(Configurable up to 65534).
- **Constant deduplication**: For both numbers and strings.
- **Optimized global variable access**: Achieves `O(1)` time complexity, the access overhead is close to that of local variables. With dynamic update key indexes, direct index fetching can be achieved in almost all cases. Indexes are rarely invalidated, unless you frequently declare new global variables.
- **Optional object header compression**: Object headers are compressed from 16 bytes to 8 bytes by compressing the 64-bit pointer to 48 bits.
- **Optional NaN Boxing**: Compress the generic type value from 16 bytes to 8 bytes(from clox).
- **Inline `init()`**: The inline caching class init() method helps reduce the overhead of object creation.
- **Flip-up GC marking**: Flipping tags can avoid reverting to the write of tags during the recycling process, and favor concurrent tags (if actually implemented).
- **Detached static and dynamic objects**: Static objects such as strings/functions, they don't usually bloat very much, so I think it's a viable option not to recycle them.
- **Compilation-time optimizations**: Provides basic constant folding and super instruction.
- **Instruction Dispatching**: Use `direct threading code` instead of `switch case` in compilers that support compute goto(clang & gcc).

---

#### Performance test — v0.9.9-dev 

_(AMD Ryzen7-5800X, Windows 11, ClangCL/LLVM 19)_
|program|loxFlux|clox|
|---|---|---|
|fib30|52ms|84ms|
|fib35|575ms|930ms|
|fib40|6420ms|10254ms|
|loop 1e8|728ms|1109ms|
|global loop 1e8|860ms|2044ms|
|binary_trees|1548ms|2650ms|
|equality|1290ms|2107ms|
|instantiation|391ms|1067ms|
|invocation|212ms|249ms|
|method_call|140ms|179ms|
|properties|307ms|400ms|
|string_equality|471ms|NaN: Too many constants in one chunk|
|trees|1677ms|3560ms|
|zoo|246ms|298ms|
|zoo_batch|6630batch|5152batch|
---

### Comment

- **Line & Block comment support**: Using `//` `/* */`.

---

### Numbers and Strings

- **Binary literal**: Use `0b` or `0B` prefix, e.g., `0b1010`.
- **Hexadecimal literal**: Use `0x` or `0X` prefix, e.g., `0xFF`.
- **Scientific notation**: Supports formats like `1.2e+3` and `123E-2`.
- **Escape characters**: Supports escaping with backslash `\`, such as `\"` for double quotes; Example: `"\"hello world\""` renders as `"hello world"`.

---

### Variable

- **Allows multiple definitions of variable constants**: (e.g., `var a = 1,b = 2,c = a + b;`).
- **`const` keyword support**: Supported within blocks.

---

### Loop

- **do-while**: Supported `while`,`for` and `do-while` loop.
- **`break` and `continue` keywords**: Supported within loops.

---

### New Branch Syntactic

```ebnf
branchState ::= "branch" "{" caseState "}"
caseState  ::= (condState|noneState) | (condState+ noneState?)
condState  ::= condition ":" statement
noneState  ::= "none" ":" statement
```

- **`branch`**: branch: This statement block simplifies `if-else if` chains and serves as an alternative to `switch-case` statements. The `none` branch (equivalent to default in switch statements) must appear last.

---

### Bitwise Operations

- **Bitwise AND (`&`)**  
  Performs a logical AND on each pair of corresponding bits. Returns an integer result.  
  **Example**: `0b1010 & 0b1100` evaluates to `0b1000` (decimal `8`).  

- **Bitwise OR (`|`)**  
  Performs a logical OR on each pair of corresponding bits. Returns an integer result.  
  **Example**: `0b1010 | 0b1100` evaluates to `0b1110` (decimal `14`).  

- **Bitwise XOR (`^`)**  
  Performs a logical exclusive OR (XOR) on each pair of corresponding bits. Returns an integer result.  
  **Example**: `0b1010 ^ 0b1100` evaluates to `0b0110` (decimal `6`).  

- **Left Shift (`<<`)**  
  Shifts the bits of the first operand left by the number of positions specified by the second operand.  
  **Example**: `0b0001 << 2` evaluates to `0b0100` (decimal `4`).  

- **Right Shift (`>>`)** (Sign-propagating)  
  Shifts the bits right, preserving the sign bit (arithmetic shift).  
  **Example**: `-8 >> 1` evaluates to `-4` (sign preserved).  

- **Unsigned Right Shift (`>>>`)** (Zero-fill)  
  Shifts the bits right, filling the leftmost bits with `0` (logical shift).  
  **Example**: `-8 >>> 1` evaluates to `2147483644` (zero-fill, no sign preservation).  

  #### Notes:
  1. **Shift Behavior**:  
    - **Negative shifts**: Negative shift values always return `0`.  
    - **Large shifts**: Shifts beyond `31` bits are truncated via `shiftAmount % 32`.  
  2. **Operands**: All operations convert operands to 32-bit integers before execution.  

---

### Lambda
- **Lambda Syntax**: An anonymous function can be declared inline using the `lambda` keyword. This provides a concise way to define functions without explicitly naming them, making it suitable for short, inline operations.
- **Syntax**: The syntax supports both block form `lambda (parameters) { ... }` for multi-statement bodies and arrow form `lambda (parameters) => expr` for single-expression returns, where `parameters` are input arguments and the body contains the logic to be executed.
- **Use Cases**: Lambda expressions are commonly used in functional programming patterns, such as passing functions as arguments to higher-order functions (e.g., `map`, `filter`, `reduce`).

---

### Array

- **Array Literals**: Supports defining array literals directly in the code(no more than `1024` within each `[]`)(Configurable up to 65535), making array creation more intuitive and convenient(nesting is supported).
- **Syntax**: Use square brackets `[]` to define an array. Elements are separated by commas. Arrays can hold elements of any supported data type, including numbers, strings, objects, or even other arrays (nested arrays).
- **Typed Array**: Typed arrays are pure arrays in compiled languages like C/CPP as we know them, and if you try to assign a non-numeric type to it, it will become `0`.

---

### Instance

- **Object Literals**: Supports defining object literals directly with `{k1:v1,"k2":v2}`,making object creation more intuitive and convenient(Nesting is supported).
- **Delete property**: Remove key-value pairs by assigning nil to the object.
- **`instanceOf` keyword**:  Checks if an object is an instance of a specific class.
- **`typeof` keyword**: Returns the string of item's subdivision type.

---

### Subscript

- **Indexing Syntax**: Access and modify array elements using subscript notation with square brackets.
- **Bounds Checking**: Automatically checks for out-of-bounds, when you access an array out of bounds, it doesn't throw an error, but returns a `nil`.
- **Zero-based Indexing**: The first element of the array is accessed with index `0`, the second with index `1`, and so on.
- **Assignment via Subscript**: Modify array elements by assigning new values using the subscript operator.

- **Object Key Access**: In addition to arrays, subscript notation also supports accessing object properties by key.
- **Syntax**: Use square brackets `[]` with a string representing the key name.
- **Assignment via Subscript for Objects**: Modify object properties by assigning new values using the subscript operator.

---

### Module System

supports loading and compiling modules from files, allowing for code organization and reuse. The module system provides a simple yet powerful way to split your code across multiple files.(Note that cyclic reference handling will not be performed).

- **import**: Loads, compiles, and executes a script from the file system, then returns whatever the module exports. The imported file path can be a string literal or a variable. It will get absolute path with each call and cache the mapping of paths to scripts to avoid unnecessary compilation behavior.
```
var thing = import "./module.lfx";  
print thing; // Prints the exported value from module.lfx  
```
- **export**: Used within a module file to specify what value should be returned to the importing file. It works similarly to `return` but in the module context.
```
// Inside module.lox  
const PI = 3.14159;  // the moudle file work in local scope, so you can use const
  
// Export an object with math functions  
export {  
  "pi": PI,  
  "multPI": lambda(a) => a * PI  
};
```

---

### Built-in Modules

There are some namespace objects that start with `'@'` available, and since they are not in the global scope, the initial state of the global scope is a "completely clean" state. 

#### `@math` `@array` `@object` `@string` `@time` `@ctor` `@sys`

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
  - `random`: Generates a pseudo-random number using the `xoshiro256**` algorithm.
  - `seed`: Initializes the random number generator with a specific seed value.

- **Special Value Checks**:
  - `isNaN`: Checks if a value is NaN (Not a Number).
  - `isFinite`: Checks if a value is finite (not infinite or NaN).

These functions are designed to provide robust mathematical capabilities while maintaining high performance through native implementation. They are ideal for scientific computations, game development, simulations, and other domains requiring precise numerical operations.

---

The `@array` module provides robust support for working with arrays, enabling efficient manipulation of collections of data. These utilities allow developers to create and manage arrays of various types with fine-grained control.

- **Array Methods**:
  - `resize`: Resizes an existing array to a new length. If the new length is larger, additional elements are initialized to zero (or the default value for the type). If smaller, excess elements are discarded.
  - `length`: Returns the current number of elements in the array.
  - `pop`: Removes and returns the last element of the array. If the array is empty, it may return nil or throw an error, depending on configuration.
  - `push`: Appends one or more elements to the end of the array.
  - `slice`: Extracts a section of a array and returns it as a new array, supporting negative indices.

These utilities are invaluable for working with structured data, especially in performance-critical applications or environments where memory usage must be tightly controlled. They enable developers to manage arrays explicitly and efficiently.

---

The `@object` module provides utilities for type checking and object introspection. These functions are essential for determining the nature of values and ensuring type safety in dynamic environments.

- **Type Checking**:
  - `isClass`: Verifies if a value is a class.
  - `isFunction`: Verifies if a value is a function|native-function.
  - `isObject`: Verifies if a value is an object.
  - `isArray`: Verifies if a value is an array.
  - `isArrayLike`: Verifies if a value is array or typedArray.
  - `isTypedArray`: Verifies if a value is typedArray.
  - `isString`: Verifies if a value is a string.
  - `isStringBuilder`: Verifies if a value is a stringBuilder.
  - `isNumber`: Verifies whether a value is a number.
  - `isBoolean`: Verifies whether a value is true or false.
  - `getGlobal`: Get global variable with a string key.
  - `setGlobal`: Set or define global variable with a string key.
  - `keys`: Returns the own keys array of an instance.

These functions are particularly useful for runtime type validation and debugging, allowing developers to write robust and error-resistant code.

---

The `@string` module provides advanced string manipulation capabilities, supporting both basic operations and high-performance string building. It handles ASCII and UTF-8 encodings with memory-efficient strategies, ideal for text processing tasks.

- **String Methods**:
  - `length`: Returns the byte length of a string.  
  - `utf8Len`: Returns the character count for UTF-8 strings (ignoring byte-level details). e.g.,`@string.utf8Len("αβγ")` → `3`
  - `charAt`: Retrieves an ASCII character by byte position.  
  - `utf8At`: Retrieves a UTF-8 character by logical character position. e.g.,`@string.utf8At("αβγ", 1)` → `"β"`
  - `append`: Efficiently appends strings or other builders to a `StringBuilder`.
  - `intern`: Converts a `StringBuilder` to an immutable string(will occupy the constant scale), or returns existing strings directly.
  - `equals`: Compare whether the content of two strings|stringBuilders is the same.
  - `slice`: Extracts a section of a string or StringBuilder and returns it as a new StringBuilder, supporting negative indices.
  - `parseInt`: Parses string to integer (supports hex/octal/binary prefixes)	and base(2 to 36).
  - `parseFloat`: Parses string to float (supports scientific notation).

This module balances performance and safety for both simple text tasks and large-scale string processing.

---

The @time module provides precise timing functions for measuring and working with time intervals. These functions are implemented as native bindings for efficiency and accuracy, making them suitable for performance-critical applications, benchmarking, and timestamp generation.

- **Time Methods**:
  - `nano`: Returns the current time in nanoseconds (1e-9 seconds) since an arbitrary reference point (e.g., system startup).
  - `micro`: Returns the current time in microseconds (1e-6 seconds) since an arbitrary reference point.
  - `milli`: Returns the current time in milliseconds (1e-3 seconds) since an arbitrary reference point.
  - `second`: Returns the current time in seconds since an arbitrary reference point.
  - `utc`: Returns the current UTC time in milliseconds since the Unix epoch (January 1, 1970).

These utilities enable precise time management in applications requiring performance optimization or temporal coordination.

---

The `@ctor` moudle provides built-in types of constructors.

- **Constructors**:
  - `Object`: Creates an empty object that can hold string-value pairs of any supported type.
  - `Array`: Creates a generic dynamic array that can hold elements of any supported type.
  - `F64Array`: Creates a fixed-size array of 64-bit floating-point numbers (IEEE 754 double precision).
  - `F32Array`: Creates a fixed-size array of 32-bit floating-point numbers (IEEE 754 single precision).
  - `U32Array`: Creates a fixed-size array of 32-bit unsigned integers.
  - `I32Array`: Creates a fixed-size array of 32-bit signed integers.
  - `U16Array`: Creates a fixed-size array of 16-bit unsigned integers.
  - `I16Array`: Creates a fixed-size array of 16-bit signed integers.
  - `U8Array`: Creates a fixed-size array of 8-bit unsigned integers (commonly used for byte-level operations).
  - `I8Array`: Creates a fixed-size array of 8-bit signed integers.
  - `StringBuilder`: Creates a mutable string buffer, optionally initialized with a string or another builder.  

---

The `@sys` module offers low-level system utilities, primarily focused on memory management and garbage collection. These functions provide insights into the runtime environment and allow fine-grained control over resource allocation.

- **Io**
  - `log`: Unlike the `print` keyword, it allows for multiple inputs and behaves slightly differently.It automatically expands the contents of the array and prints (but not recursively).
  - `error`: Output string or stringBuilder information to stderr.
  - `input`: Read a line of input from the console and return a stringBuilder.
  - `readFile`: Reads the contents of a file and returns them as a StringBuilder object. 

- **Garbage Collection**:
  - `gc`: Triggers a full garbage collection cycle.
  - `gcNext`: Configure the heap memory usage to be used for the next GC trigger.
  - `gcBegin`: Configure the limits of the initial GC.

- **Memory Statistics**:
  - `allocated`: Returns the total number of bytes currently allocated in the dynamic memory pool(includes built-in objects and deduplication pools).
  - `static`: Returns the total number of bytes allocated for static objects(e.g., strings, functions).

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
The project **loxFlux** is based on `MIT` and uses two third-party projects.
  - Copyright (c) 2025 IMSDCrueoft
  - License: `MIT`

1. **mimalloc**
   - Copyright (c) 2018-2025 Microsoft Corporation, Daan Leijen
   - License: `MIT`

2. **xxHash**
   - Copyright (c) 2012-2021 Yann Collet
   - License: `BSD 2-Clause`

## Other expectations
1. try catch syntax.
2. just in time(LLVM or CraneLift).