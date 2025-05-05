# LoxFlux

LoxFlux is developed based on the cLox version of the Lox language (stack-based bytecode-virtual machine). Please note that the project has not yet completed its basic functions.

- **Design metrics**: loxFlux should be fully compatible with the lox syntax, so lox scripts can be painlessly interpreted and executed in loxFlux (although error handling may vary, the "correct code" is the same).

## Introduction of Lox

Lox is a programming language designed for learning purposes. It is conceived as a small and easy-to-understand language, perfect for those who want to build their own interpreter or compiler. The Lox language and its associated tutorials were originally introduced by Bob Nystrom in his online book "Crafting Interpreters". ([Book URL](https://craftinginterpreters.com/))

## Resources of Lox

- [Crafting Interpreters - Official Website](https://craftinginterpreters.com/)
- [Crafting Interpreters - Official Github](https://github.com/munificent/craftinginterpreters)

## List of LoxFlux features

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

### Variable

- **Allows multiple definitions of variable constants**: (e.g., `var a = 1,b = 2,c = a + b;`).

#### Global Variable

- **Optimized global variable access**: Achieves `O(1)` time complexity. With dynamic index updates, direct index fetching can be achieved in almost all cases. Indexes are rarely invalidated, unless you frequently delete and then declare global variables that don't exist.
- **Global instance**: Use `@global` to explicitly get the global table, create and delete attributes, use it like a simple object.

---

#### Local Variable

- **Local variable range**: Expands to support up to 1023 nested variables(Configurable up to 65534).
- **`const` keyword support**: Supported within blocks.

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
- **Syntax**: The syntax for a lambda expression is `lambda (parameters) { body }`, where `parameters` are the input arguments and `body` contains the logic to be executed.
- **Use Cases**: Lambda expressions are commonly used in functional programming patterns, such as passing functions as arguments to higher-order functions (e.g., `map`, `filter`, `reduce`).

---

### Array

- **Array Literals**: Supports defining array literals directly in the code(no more than `1024` within each `[]`)(Configurable up to 65535), making array creation more intuitive and convenient(nesting is supported).
- **Syntax**: Use square brackets `[]` to define an array. Elements are separated by commas. Arrays can hold elements of any supported data type, including numbers, strings, objects, or even other arrays (nested arrays).
- **Typed Array**: Typed arrays are pure arrays in compiled languages like C/CPP as we know them, and if you try to assign a non-numeric type to it, it will become `0`.

---

### Instance

- **Object Lierals**: Supports defining object literals directly with `{k1:v1,"k2":v2}`,making object creation more intuitive and convenient(Nesting is supported).
- **Delete property**: Remove key-value pairs by assigning nil to the object.
- **`instanceOf` keyword**:  Checks if an object is an instance of a specific class.
- **`typeof` keyword**: Returns the string of item's subdivision type.
- **`init()`**: Inline caching class init() method.

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

### Loop

- **do-while**: Supported `while`,`for` and `do-while` loop.
- **`break` and `continue` keywords**: Supported within loops.

---

### Branch Syntactic

```ebnf
branchState ::= "branch" "{" caseState "}"
caseState  ::= (condState|noneState) | (condState+ noneState?)
condState  ::= condition ":" statement
noneState  ::= "none" ":" statement
```

- **`branch`**: branch: This statement block simplifies `if-else if` chains and serves as an alternative to `switch-case` statements. The `none` branch (equivalent to default in switch statements) must appear last.

---

### Comment

- **Block comment support**: Using `/* */`.

---

### Optional object header compression switch

- **48-bit pointer**: Compress the object header from 16 bytes to 8 bytes.

---

### GC

- **Detached static objects and dynamic objects**: Static objects such as strings/functions, they don't usually bloat very much, so I think it's a viable option not to recycle them.

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
  - `random`: Generates a pseudo-random number using the WellRng1024a algorithm.
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
  - `isBoolean`: Verifies whether a value is true|false.

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

- **Log**
  - `log`: Unlike the `print` keyword, it allows for multiple inputs and behaves slightly differently.It automatically expands the contents of the array and prints (but not recursively).

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

## Other
LLVM/CraneLift may be used as a JIT in the future.
