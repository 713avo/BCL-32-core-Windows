# BCL-32 Language Manual

**Basic Command Language 32 — kernel v1.2**

Reference and user guide for the BCL-32 language as implemented by the core
interpreter (`bcl`). It is based on the book *Manual de BCL-32* and cross-checked
against the kernel specification and the interpreter itself: every example in this
document has been executed and the output shown is the real one (each `bcl` block is
followed, where relevant, by a `text` block with what it prints).

> **Scope.** This manual covers the **kernel** only: the language and its built-in
> commands. Binary modules (`.bll`: Linux system access, JSON, databases, graphical
> interfaces, etc.) are distributed separately and are not documented here; only the
> way the kernel loads them is explained (section 11). For that reason no example uses
> module commands (files, timers, network…).

## Table of contents

- [1. Introduction](#1-introduction)
- [2. Quick reference](#2-quick-reference)
- [3. Language fundamentals](#3-language-fundamentals)
- [4. The `CALC` evaluator](#4-the-calc-evaluator)
- [5. Variables](#5-variables)
- [6. Input and output](#6-input-and-output)
- [7. Control structures](#7-control-structures)
- [8. Associative arrays](#8-associative-arrays)
- [9. Lists](#9-lists)
- [10. Program arguments](#10-program-arguments)
- [11. Modules, `SOURCE` and the startup environment](#11-modules-source-and-the-startup-environment)
- [12. Procedures and scope](#12-procedures-and-scope)
- [13. Strings](#13-strings)
- [14. Error handling](#14-error-handling)
- [15. Exercises and idioms](#15-exercises-and-idioms)
- [Appendix: known kernel defects (build 7)](#appendix-known-kernel-defects-build-7)

## 1. Introduction

### 1.1 What is BCL?

**BCL** (*Basic Command Language*) is a minimalist interpreted programming language
built on the idea that **everything is a text string**. It is a command-oriented
*scripting* language, similar in spirit to Tcl, with a direct and predictable syntax.

Main features:

- Every value is a string: there are no integer, decimal or list types.
- Command syntax with a fixed number of arguments.
- Keywords and command names are case-insensitive.
- Variables are read with the `$` prefix.
- Arithmetic and numeric logic are delegated to the `CALC` command.
- Indices start at **1** (strings, lists, vectors).
- It is extended at run time with binary modules (`LOAD`).

### 1.2 The «everything is a string» philosophy

```bcl
SET age 25
```

The variable `age` does not hold the number 25: it holds the two-character string
`"25"`. When arithmetic is needed, `CALC` interprets the string as a number and returns
the result, which is also a string. There are no type-conversion errors and behaviour is
always predictable.

| Concept           | In other languages            | In BCL                                  |
|-------------------|-------------------------------|-----------------------------------------|
| Assign a variable | `x = 10`                      | `SET x 10`                              |
| Addition          | `x = a + b`                   | `SET x [CALC $a + $b]`                  |
| Print             | `print("Hello")`              | `PUTS "Hello"`                          |
| Conditional       | `if (cond) {...}`             | `IF cond THEN ... END`                  |
| `while` loop      | `while (cond) {...}`          | `IF cond DO ... LOOP`                   |
| Function          | `def f(a, b): ...`            | `PROC f WITH a b DO ... END`            |
| Error handling    | `try: ... except: ...`        | `TRY ... CATCH e ... END`               |

### 1.3 How the interpreter works

Code is not compiled: a lexer splits it into tokens, the parser builds a tree (AST) and
the evaluator walks it executing each command. The AST has only **two control
structures**: `IF … THEN` and the `IF … DO` loop (plus `BREAK` and `NEXT`).

### 1.4 Running programs

```bash
bcl program.bcl                  # script mode
bcl program.bcl arg1 arg2        # with arguments (variable $ARGS)
bcl                              # interactive mode (REPL)
bcl --version                    # version and build number
```

Command-line options:

| Option                | Effect                                                                 |
|-----------------------|------------------------------------------------------------------------|
| `--version`           | Prints the kernel version and build, then exits.                       |
| `--no-rc`             | Does not read the startup file `$HOME/.bcl.rc` (same as `BCL_NO_RC=1`). |
| `--restricted`        | Restricted mode (same as `BCL_RESTRICTED=1`): no `LOAD`/`UNLOAD`.      |
| `--source-root DIR`   | In restricted mode, root outside of which `SOURCE` fails (`BCL_SOURCE_ROOT`). |
| `--`                  | End of options; what follows is the script.                            |

The full syntax is `bcl [--restricted] [--no-rc] [--source-root DIR] [--] [script.bcl [args...]]`.

The REPL shows a prompt with the version and is closed with `exit`, `quit`,
`EXIT <code>` or `Ctrl+D`:

```text
BCL-32 1.2> SET name "Ann"
BCL-32 1.2> PUTS "Hello, $name"
Hello, Ann
BCL-32 1.2> exit
```

An uncaught error in a script prints the diagnostic on standard error and terminates
with exit code **1**.

### 1.5 Your first program

```bcl
# My first BCL-32 program
PUTS "Hello, World"
```
```text
Hello, World
```

The first line is a comment; `PUTS` prints its argument followed by a newline. By
convention files use the `.bcl` extension.

### 1.6 Basic structure of a program

A program is a sequence of commands, **one per line**: command name and arguments.
Square brackets `[...]` run the inner command and use its result as an argument
(section 3.4).

```bcl
# Format: COMMAND arg1 arg2 ...
SET x 42                    # assign a variable
PUTS "The value is: $x"     # print with a variable
SET y [CALC $x * 2]         # compute and store the result
PUTS "Twice that is: $y"
EXIT 0                      # end the program
```
```text
The value is: 42
Twice that is: 84
```

## 2. Quick reference

A summary for quick lookup; each point is explained in detail in the following sections.

### 2.1 General rules

1. Every value is a **text string**. There are no types.
2. All assignment is done with `SET`; all arithmetic with `CALC`.
3. **Indices start at 1** in strings, lists and vectors. The value `0` means «no
   result».
4. False is `0` and the empty string; **any other value is true**.
5. Command names are **case-insensitive**; variable names are not.
6. Each block keyword takes its own line: there is no one-line `IF … THEN … END`.
7. Reading a nonexistent variable returns `""` and is **not** an error.
8. A variable name continues as long as there are letters, digits, `_` or `:`; any
   other character ends it.
9. No command modifies its arguments: all of them return a new value.

### 2.2 Kernel commands

| Command | Arguments | Description |
|---|---|---|
| **Variables** | | |
| `SET` | `name value…` | Assigns a variable or array element. With several values, joins them with spaces |
| `INCR` | `name [increment]` | Adds 1 (or the increment, negative or decimal) to the variable |
| `INCRV` | `name increment` | Adds the given increment (not validated) |
| `EXISTS` | `name` | `1` if the variable or element exists |
| `UNSET` | `name` | Removes a variable, element or whole array |
| `GLOBAL` | `name` | Links **one** name to the global scope |
| **Arrays** | | |
| `ARREXISTS` | `array` | `1` if the array has any element |
| `ARRLENG` | `array` | Number of elements |
| `ARRKEYS` | `array` | List of keys (order not guaranteed) |
| `ARRGET` | `array` | Flat list `key value key value…` |
| `ARRSET` | `array list` | Loads pairs from a flat list |
| `ARRDEL` | `array key` | Removes an element (silent) |
| `ARRCLEAR` | `array` | Empties the array |
| **Input/output** | | |
| `PUTS` | `string…` | Prints with a newline |
| `PUTSN` | `string…` | Prints without a newline |
| `GETS` | — | Reads one line from standard input |
| **Arithmetic** | | |
| `CALC` | `expression` | Evaluates a mathematical or logical expression (real or complex domain) |
| **Strings** (base 1, per character) | | |
| `STRLENG` | `str` | Length in characters |
| `STREQ` | `str1 str2` | `1` if identical, `0` otherwise |
| `STRGET` | `str pos` | Character at `pos`, or `""` |
| `STRRANG` | `str start end` | Substring, both ends included |
| `STRAPP` | `str1 str2` | Concatenation (exactly two arguments) |
| `STRINS` | `str text pos` | Inserts `text` starting at `pos` |
| `STRDEL` | `str pos count` | Deletes `count` characters from `pos` |
| `STRFIND` | `str sub` | List of all positions, or `0` |
| `STRFMT` | `"fmt" [val]` | Formats with **one** conversion |
| `STRUPP` / `STRLOW` | `str` | Upper / lower case (ASCII only) |
| `STRREV` | `str` | Reverses the string (correct UTF-8) |
| **Lists** (base 1) | | |
| `LSTLENG` | `list` | Number of elements |
| `LSTGET` | `list pos` | Element at `pos`, or `""` |
| `LSTRANG` | `list start end` | Sublist, both ends included |
| `LSTAPP` | `list1 list2` | Concatenates **two lists** |
| `LSTINS` | `list1 list2 pos` | Inserts `list2` before `pos` |
| `LSTDEL` | `list pos count` | Removes `count` elements from `pos` |
| `LSTFIND` | `list elem` | List of all positions, or `0` |
| `LSTSORT` | `list` | Sorts: numbers by value, then text ignoring case |
| `LSTFMT` | `text sep` | **Splits** `text` on `sep` and returns the list |
| **Flow control** | | |
| `IF … THEN … END` | `cond` | Conditional, with optional `ELSEIF` and `ELSE` |
| `IF … DO … LOOP` | `cond` | Loop; the optional `ELSE` runs if the condition runs out without `BREAK` |
| `BREAK` | — | Leaves the nearest loop |
| `NEXT` | — | Jumps to the next iteration |
| **Procedures** | | |
| `PROC` | `name [WITH p… @opt…] DO … END` | Defines a procedure |
| `RETURN` | `[value]` | Returns a value and finishes; without it `""` is returned |
| **Errors** | | |
| `TRY … CATCH v … END` | | Catches errors; `v` receives the text and `v_INFO(...)` the fields |
| `ERROR` | `"message"` | Raises a catchable error |
| **Modules and scripts** | | |
| `SOURCE` | `"path.bcl"` | Runs a script in the current scope |
| `SOURCEDIR` | — | Directory of the file being evaluated |
| `LOAD` / `UNLOAD` | `"path.bll"` | Loads / unloads a binary module |
| **System** | | |
| `EXIT` | `code` | Ends the program with that exit code |

Language keywords: `SET IF THEN ELSEIF ELSE END DO LOOP BREAK NEXT PROC WITH
RETURN GLOBAL TRY CATCH ERROR LOAD UNLOAD SOURCE EXIT INCR INCRV EXISTS UNSET`.

> The event commands (`INT`, `TIMER`, `RTIMER`, `RUN`, `CANCELINT`, `CANCELTIMER`,
> `EVTDATA`, `EVTSOURCE`) are **not** part of the kernel: they are registered by the
> platform module loaded with `LOAD`.

### 2.3 Escape sequences

Valid inside quoted strings (and as bare arguments):

| Sequence | Result | Sequence | Result |
|---|---|---|---|
| `\n` | Newline | `\\` | Backslash |
| `\t` | Tab | `\"` | Double quote |
| `\r` | Carriage return | `\$` | Literal `$` sign |
| `\a` `\b` `\f` `\v` | Bell, backspace, form feed, vertical tab | `\[` `\]` | Literal square brackets |
| `\xHH` | Character in hexadecimal | `\#` | Literal hash |
| `\uHHHH` | Unicode character | `\0dd` | Character in octal |

### 2.4 `STRFMT` and `CALC` at a glance

`STRFMT`: form `%[flags][width][.precision]type`, with **exactly one** specifier (or
none). Types: `s d i u f e E g G o x X c %%`. Flags: `-` (left), `0` (zeros), `+` (sign),
`#` (base prefix). Width and precision ≤ 4096; no `%n`, `*` or `%ld`.
Details in section 13.5.

`CALC`: operators from highest to lowest precedence: functions `f(x)`; unary `-` and `!`;
`**` (right to left); `* / %`; `+ -`; `<< >>`; `< > <= >=`; `== !=`; `&`; `^`; `|`;
`&&`; `||`. Real functions: `abs acos asin atan atan2 bool ceil cos cosh double entier
exp floor fmod hypot int isqrt log log10 max min pow rand round sin sinh sqrt srand tan
tanh wide`. Complex: `complex real imag conj arg polar norm` (and several of the
above extend to complex numbers). Bases: `255`, `0xFF`, `0377`, `0b1111`, `2.55e2`.
Details in section 4.

### 2.5 Kernel diagnostics

Every error header has the form `?FAC-SEV-CODE, summary`: `FAC` is the subsystem, `SEV`
the severity (`I` informational, `W` warning, `F` fatal catchable with `TRY`/`CATCH`,
`U` unconditional fatal) and `CODE` a stable code.

| Diagnostic | What it usually means |
|---|---|
| `?PAR-F-MISSTERM` | A `END` or `LOOP` is missing; also appears with a one-line `IF` |
| `?LEX-F-UNTERMSTR` / `?LEX-F-UNTERMSUB` | Unclosed quote or bracket |
| `?EVAL-F-UNKCMD` | Unknown command or procedure |
| `?EVAL-F-BADARGS` | Wrong number of arguments |
| `?EVAL-F-DEPTH` | Nesting or recursion without a base case |
| `?EVAL-F-USERERR` | Error raised by your own `ERROR` |
| `?CALC-F-UNKNOWN` | `CALC` received something that is not a number |
| `?CALC-F-DOMAIN` | Division or modulo by zero |
| `?CALC-F-SYNTAX` / `?CALC-F-BADVALUE` | Malformed expression / operand out of domain |
| `?SET-F-DUPLICATE` | A name is used both as a scalar and as an array |
| `?UNSET-F-BADVALUE` | `UNSET` on an active `PROC` parameter |
| `?STRFMT-F-BADARGS` | The template does not have exactly one slot, or extra values |
| `?STRFMT-F-UNSUPPORTED` | Unsupported conversion or modifier |
| `?ARRSET-F-BADARGS` | Odd number of elements in the pair list |
| `?LIST-F-SYNTAX` | Unbalanced braces in a list |
| `?SRC-F-NOFILE` | `SOURCE` cannot find the file |
| `?LOAD-F-NOTFOUND` / `?LOAD-F-OPENFAIL` | Module not found in `BCL_LIBS` / cannot be opened |
| `?LOAD-F-RESTRICTED` | `LOAD`/`UNLOAD` in restricted mode |

### 2.6 Strings, lists and arrays: equivalences

| Operation | String | List | Array |
|---|---|---|---|
| Size | `STRLENG` | `LSTLENG` | `ARRLENG` |
| Get one element | `STRGET` | `LSTGET` | `$arr(k)` |
| Range | `STRRANG` | `LSTRANG` | — |
| Search | `STRFIND` | `LSTFIND` | `EXISTS` |
| Concatenate | `STRAPP` | `LSTAPP` | `SET arr(k) v` |
| Insert | `STRINS` | `LSTINS` | — |
| Delete | `STRDEL` | `LSTDEL` | `ARRDEL` |
| Empty | — | — | `ARRCLEAR` |
| Sort | — | `LSTSORT` | — |
| Serialize | — | — | `ARRGET` |
| Deserialize | — | `LSTFMT` (splits text) | `ARRSET` |

### 2.7 Reserved variables

| Variable | Contents |
|---|---|
| `$ARGS` | Canonical list with the command-line arguments. Keeps empty arguments, spaces and braces. Always exists; with no arguments it is the empty list. |
| `$BCL_LIBS` | Directories (comma-separated) where `LOAD` looks for modules named without `/`. Initial value: the environment variable of the same name. |

### 2.8 Program template

```bcl
#!/usr/bin/env bcl
# ============================================================
#  name.bcl --- what this program does
#  Usage: name.bcl <arguments>
# ============================================================

# --- 1. Modules and libraries -------------------------------
# LOAD   module.bll
# SOURCE utils.bcl

# --- 2. Configuration ---------------------------------------
ARRSET config "verbose 0 output /tmp/output.txt"

# --- 3. Procedures ------------------------------------------
PROC usage DO
    PUTS "Usage: name.bcl <arguments>"
END

PROC main DO
    GLOBAL config
    PUTS "Working (verbose=$config(verbose))..."
END

# --- 4. Main program ----------------------------------------
IF [LSTLENG $ARGS] == 0 THEN
    usage
    EXIT 1
END

main
EXIT 0
```
```text
Working (verbose=0)...
```

## 3. Language fundamentals

### 3.1 Comments and case

A comment starts with `#` (outside a quoted string) and runs to the end of the line.
**Commands and keywords** are case-insensitive; **variable names** and strings are
case-sensitive. The convention is to write commands in UPPER CASE.

```bcl
# Full-line comment
puts "Hello"                     # lower-case command: valid
PUTS "The # symbol does not start a comment here"
SET Name "Ann"
SET name "John"
PUTS $Name $name                 # different variables
```
```text
Hello
The # symbol does not start a comment here
Ann John
```

### 3.2 Structure of a command line

Each statement takes **one line**: `COMMAND arg1 arg2 …`. Each command expects an exact
number of arguments; if too few or too many are given, the interpreter raises an error
(`?EVAL-F-BADARGS`). Arguments containing spaces are enclosed in double quotes; quotes
are optional when there are no spaces.

```bcl
PUTS "This is a text with several spaces"
SET x "10"          # same as: SET x 10
```

`SET` and `PUTS`/`PUTSN` accept several arguments and join them with a space; almost
all other commands require a fixed number.

### 3.3 Variable substitution `$`

Before running a command, the interpreter replaces `$name` with the variable's value,
both in bare arguments and inside quoted strings.

```bcl
SET city "Madrid"
PUTS "Welcome to $city"
PUTS $city
```
```text
Welcome to Madrid
Madrid
```

If the variable **does not exist**, it is replaced by the empty string and there is no
error. A misspelled name is therefore not detected on its own; use `EXISTS` when it
matters:

```bcl
PUTS "value=<$undefined>"
IF [EXISTS config_path] THEN
    PUTS $config_path
ELSE
    PUTS "No path has been configured"
END
```
```text
value=<>
No path has been configured
```

#### Where a variable name ends

After a `$`, the name continues as long as there are **letters, digits, `_` and `:`**.
Any other character (space, `.`, `,`, `-`, `/`, quotes, `\`…) ends it.

```bcl
SET total 99
SET dir /tmp/data
PUTS "The total is $total"
PUTS "The total is $total."
PUTS "Total: $total, thanks"
PUTS "Sum$total"
PUTS "$total-1"
PUTS "$dir/report.txt"
PUTS "$total\nnext line"
```
```text
The total is 99
The total is 99.
Total: 99, thanks
Sum99
99-1
/tmp/data/report.txt
99
next line
```

> **Trap: the colon and the underscore do stick to the name.** The colon is reserved
> for compound names such as `$module:counter`:

```bcl
SET a 3
SET b 5
PUTS "$a:$b"                  # looks up the variable "a:", which does not exist
PUTS [STRAPP $a [STRAPP : $b]]
```
```text
5
3:5
```

The `${name}` form does not exist: it is printed literally. To separate a name from a
following `_` or `:`, build the string with `STRAPP`.

### 3.4 Command substitution `[...]`

A command in square brackets is executed and its **result** replaces the brackets.
They can be nested and used inside quoted strings.

```bcl
SET result [CALC 3 + 4]
PUTS "3 + 4 = $result"
PUTS [CALC 10 * 10]
PUTS [STRLENG "hello"]
SET x [CALC [STRLENG "hello"] + 1]
PUTS "x is $x and the length of 'world' is [STRLENG world]"
```
```text
3 + 4 = 7
100
5
x is 6 and the length of 'world' is 5
```

Nesting is limited to protect the stack; exceeding the limit raises `?EVAL-F-DEPTH`. A
command **without** brackets discards its result: `STRUPP $s` does not change `$s`; you
must write `SET s [STRUPP $s]`.

### 3.5 Escape sequences

```bcl
PUTS "First line\nSecond line"
PUTS "Name:\tAnn"
PUTS "She said: \"hello\""
PUTS "The price is \$5.00"
PUTS "\x41\x42\x43"
PUTS "Literal brackets: \[x\]"
SET n world
PUTS "hello $n\nbye"          # an escape after a variable is an escape
```
```text
First line
Second line
Name:	Ann
She said: "hello"
The price is $5.00
ABC
Literal brackets: [x]
hello world
bye
```

A `[` inside quotes opens a command substitution: to write it literally you must use
`\[` (and `\]`). This matters in usage messages such as `"Usage: prog \[-v\] FILE"`.

### 3.6 Summary

Programs run from top to bottom; only `IF`, loops, `PROC` and errors change that order.
There are exactly three substitution mechanisms, and they happen **before** the command
runs: variable `$name`, command `[COMMAND]` and escape sequences.

## 4. The `CALC` evaluator

`CALC expression` evaluates a mathematical or logical expression in the real or complex
domain and returns the result as a string. The expression may contain spaces and
variables (`$a`). It is almost always used with command substitution.

```bcl
PUTS [CALC 3 + 5]
PUTS [CALC 15 / 4]
PUTS [CALC 17 % 5]
PUTS [CALC 2 ** 10]
SET a 12
SET b 5
PUTS [CALC $a * $b - 1]
PUTS [CALC 0xFF + 0b1010]       # hex + binary
PUTS [CALC 0777]                # octal
PUTS [CALC 1.5e3]               # scientific
PUTS [CALC 5 << 2]
PUTS [CALC 0b1100 & 0b1010]
PUTS [CALC 0b1100 | 0b1010]
PUTS [CALC 0b1100 ^ 0b1010]
```
```text
8
3.75
2
1024
59
265
511
1500
20
8
14
6
```

### 4.1 Operators and precedence

| Kind | Operators |
|---|---|
| Arithmetic | `+` `-` `*` `/` `%` `**` (power) and unary `-` |
| Comparison (result `1`/`0`) | `==` `!=` `<` `>` `<=` `>=` |
| Logical | `&&` `\|\|` `!` |
| Bitwise | `&` `\|` `^` `<<` `>>` |

The precedence table is in section 2.4. `**` is right-associative (`2 ** 3 ** 2` is
512). Shifts require a non-negative count and, for `<<`, a non-negative left operand
(otherwise `?CALC-F-BADVALUE`). When in doubt, use parentheses:

```bcl
PUTS [CALC 2 + 3 * 4]
PUTS [CALC (2 + 3) * 4]
PUTS [CALC 2 ** 3 ** 2]
PUTS [CALC -(3 ** 2)]
PUTS [CALC 5 >= 5]
PUTS [CALC 1 && 0]
PUTS [CALC !0]
```
```text
14
20
512
-9
1
0
1
```

> **Beware of `-3 ** 2`.** Unary `-` binds tighter than `**`: it is evaluated as
> `(-3) ** 2`, which goes through the complex domain and may leave a tiny imaginary
> residue (`9+2.20436e-15i`). Write `-(3 ** 2)` to get `-9`.

### 4.2 Precision and result format

`CALC` works in double-precision floating point, but **the result is written with 6
significant digits** (`%g` format). Integers longer than 6 digits are rounded in the
resulting string, and that rounded value is what ends up in variables:

```bcl
PUTS [CALC 1.0 / 3.0]
PUTS [CALC 0.1 + 0.2]
PUTS [CALC 123456 * 3]
PUTS [CALC 1234567 + 0]
PUTS [CALC 2 ** 40]
PUTS [STRFMT "%.4f" [CALC 1.0 / 3.0]]
```
```text
0.333333
0.3
370368
1.23457e+06
1.09951e+12
0.3333
```

To present results with the precision you want use `STRFMT` (section 13.5); do not use
`CALC` to accumulate exact large integers (identifiers, checksums…): that would require
implementing arithmetic on strings by hand.

### 4.3 Functions

| Group | Functions |
|---|---|
| Rounding | `floor` `ceil` `round` `int` `entier` (same as `int`) `abs` |
| Roots and powers | `sqrt` `isqrt` `pow(x,y)` `exp` `log` (natural) `log10` `hypot(x,y)` |
| Trigonometry (radians) | `sin` `cos` `tan` `asin` `acos` `atan` `atan2(y,x)` `sinh` `cosh` `tanh` |
| Conversion and others | `double` `wide` `bool` `max(x,y)` `min(x,y)` `fmod(x,y)` |
| Random | `rand()` integer 0..32767; `rand(hi)` real in [0,hi); `rand(lo,hi)`; `srand(n)` seed |
| Complex | `complex(a,b)` `real` `imag` `conj` `arg` `polar(r,t)` `norm` |

```bcl
PUTS [CALC floor(3.7)]
PUTS [CALC round(3.5)]
PUTS [CALC int(-3.9)]
PUTS [CALC isqrt(17)]
PUTS [CALC hypot(3, 4)]
PUTS [CALC max(3, 7)]
PUTS [CALC fmod(5.5, 2)]
PUTS [STRFMT "pi = %.10f" [CALC 4 * atan(1)]]
SET x 27
PUTS [STRFMT "%.1f" [CALC pow($x, 1.0 / 3.0)]]
```
```text
3
4
-3
4
5
7
1.5
pi = 3.1415900000
3.0
```

`rand()` does **not** return a decimal between 0 and 1. For an index between 1 and `n`
use `int(rand(n)) + 1`. The generator is the C library's: it is not cryptographic.

### 4.4 Complex numbers

The result is still a string in canonical format (`a+bi`, `a-bi`, `bi`, `i`, `-i`, `a`).
The token `i` **without `$`** is the imaginary unit. `==` and `!=` work with complex
numbers; `<`, `>`, `<=`, `>=` raise an error (compare moduli with `abs`).

```bcl
PUTS [CALC sqrt(-4)]
PUTS [CALC i * i]
PUTS [CALC complex(3,4)]
PUTS [CALC "3+4i" + "1-2i"]
PUTS [CALC abs("3+4i")]
PUTS [CALC conj("3+4i")]
PUTS [CALC norm("3+4i")]
PUTS [CALC "3+4i" == complex(3,4)]
PUTS [CALC log(-1)]
TRY
    PUTS [CALC "3+4i" < 5]
CATCH e
    PUTS $e
END
```
```text
2i
-1
3+4i
4+2i
5
3-4i
25
1
3.14159i
?CALC-F-DOMAIN, Ordering comparisons require real arguments
```

**Trap with complex variables:** when `$z` is substituted into an expression, the text
`1+2i` is pasted as is and precedence can change the result. Put parentheses around the
substitution:

```bcl
SET z [CALC complex(1,2)]
PUTS [CALC ($z) * ($z)]      # correct: (1+2i)^2 = -3+4i
PUTS [CALC $z * $z]          # wrong: 1 + 2i*1 + 2i*i ... = 1+4i
```
```text
-3+4i
1+4i
```

### 4.5 `CALC` errors

Division and modulo by zero **are** errors (`?CALC-F-DOMAIN`), as is operating on
non-numeric text (`?CALC-F-UNKNOWN`) or a malformed expression (`?CALC-F-SYNTAX`).

```bcl
TRY
    PUTS [CALC 1 / 0]
CATCH e
    PUTS $e
END
TRY
    PUTS [CALC hello + 1]
CATCH e
    PUTS $e
END
TRY
    PUTS [CALC 5 +]
CATCH e
    PUTS $e
END
```
```text
?CALC-F-DOMAIN, Division by zero
?CALC-F-UNKNOWN, Unknown symbol
?CALC-F-SYNTAX, Unexpected token
```

## 5. Variables

### 5.1 `SET`

`SET name value…` is the only way to assign. With several values it joins them with a
space (useful for writing literal lists). Everything is stored as a string.

```bcl
SET name "Maria"
SET age 25
SET empty ""
SET greeting "Good morning, $name"
SET list one two three
PUTS "$greeting ($age) <$empty> \[$list\]"
PUTS $list
SET age 26                    # overwrites
PUTS $age
```
```text
Good morning, Maria (25) <> [one two three]
one two three
26
```

Names accept letters, digits and `_` (and `:` for compound names), and cannot start with
a digit. Upper and lower case are distinguished: `$Total` and `$total` are different.

### 5.2 `INCR` and `INCRV`

`INCR name [increment]` adds 1 by default; the increment may be negative or decimal.
`INCRV name increment` is equivalent with the increment mandatory. A nonexistent or
non-numeric variable is taken as 0. With `INCR`, an increment that is not a whole number
(`abc`, `5x`) gives `?INCR-F-BADVALUE` and the variable does not change; `INCRV` does not
validate the increment (a non-numeric value counts as 0).

```bcl
SET c 0
INCR c
INCR c
INCR c -1
INCR c 0.5
PUTS $c
SET step 7
INCRV c $step
PUTS $c
INCR fresh
PUTS $fresh
TRY
    INCR c abc
CATCH e
    PUTS $e
END
```
```text
1.5
8.5
1
?INCR-F-BADVALUE, Increment must be a number: abc
```

Like `CALC`, both commands write the result with 6 significant digits.

### 5.3 `EXISTS` and `UNSET`

`EXISTS name` returns `1` or `0` and is used to tell «does not exist» from «is empty».
`UNSET` removes a variable, an array element (`UNSET a(k)`) or a whole array
(`UNSET a`); it does not fail if the name does not exist.

```bcl
SET x 10
PUTS [EXISTS x]
PUTS [EXISTS missing]
SET city(capital) "Madrid"
PUTS [EXISTS city(capital)]
UNSET x
UNSET never_existed
PUTS [EXISTS x]
UNSET city
PUTS [ARREXISTS city]
```
```text
1
0
1
0
0
```

Arrays (`SET a(k) v`, section 8) use names of the form `a(k)`; a name cannot be both a
scalar and an array (`?SET-F-DUPLICATE`). Variables with special meaning: `$ARGS`
(section 10) and `BCL_LIBS` (section 11).

## 6. Input and output

Three commands: `PUTS`, `PUTSN` and `GETS`.

### 6.1 `PUTS` and `PUTSN`

`PUTS` prints its arguments (joined by spaces) and a newline; `PUTSN` does the same
**without** the newline, useful for prompts and for building a line in pieces.

```bcl
PUTS "Hello, World"
PUTS "Student: Lucia" "grade" 9.5
PUTS [CALC 12 ** 2]
PUTS ""                          # blank line
PUTSN "Result: "
PUTSN [CALC 7 * 6]
PUTS " (computed)"
PUTS "Col1\tCol2\tCol3"
```
```text
Hello, World
Student: Lucia grade 9.5
144

Result: 42 (computed)
Col1	Col2	Col3
```

### 6.2 `GETS`

`GETS` reads a whole line from standard input (without the trailing newline) and returns
it. It is used with brackets. There is no fixed length limit.

```bcl
PUTSN "First: "
SET a [GETS]
PUTSN "Second: "
SET b [GETS]
PUTS ""
PUTS "The sum of $a and $b is [CALC $a + $b]"
```
```text
First: Second: 
The sum of 8 and 12 is 20
```

**End of input.** When input runs out (`Ctrl+D`, or redirected input that ends), `GETS`
returns the empty string **on every later call**; it cannot be told apart from the user
pressing Enter alone. A loop waiting for an exit word never terminates if the input ends
first. Always add an exit on empty input:

```bcl
SET n 0
IF 1 DO
    SET d [GETS]
    IF [STREQ $d ""] THEN
        BREAK                    # end of input or blank line
    END
    IF [STREQ $d "end"] THEN
        BREAK
    END
    INCR n
    PUTS "item $n -> $d"
LOOP
PUTS "read: $n"
```
```text
item 1 -> one
item 2 -> two
read: 2
```

`GETS` does not validate the type: if the user types text where you expect a number,
`CALC` will fail (use `TRY`, section 14).

## 7. Control structures

BCL has exactly two structures: the conditional `IF … THEN … END` and the loop
`IF … DO … LOOP`, with `BREAK` and `NEXT`.

> **One block keyword per line.** The one-line form `IF c THEN cmd END` does not exist;
> it gives `?PAR-F-MISSTERM, Expected block terminator before end of file`. The same
> applies to `ELSEIF`, `ELSE`, `DO`, `LOOP`, `PROC`, `TRY` and `CATCH`. The error talks
> about the end of the file, but what is usually wrong is the line where the block starts.

### 7.1 The condition

The condition of `IF` is not a `CALC` expression: it is a small language of its own.

- A **comparison** `a OP b` with `OP` among `==` `!=` `<` `>` `<=` `>=`. Each operand is
  a `$variable`, a literal (quoted or not), a `[command]` or a parenthesised group. If
  **both** operands are numbers they are compared numerically (`007 == 7` is true);
  otherwise as text. `STREQ` forces text comparison.
- A **lone value** (`IF $flag`, `IF [STRFIND $t "x"]`) is true unless it is `""` or `"0"`
  (note: `0.0` is true).
- They combine with `&&`, `||`, `!` and parentheses.
- **There is no arithmetic inside the condition.** To compute, use `[CALC …]`. Whatever
  follows the first operand is ignored **without an error**, so `IF $n % 2 == 0` is
  equivalent to `IF $n` and is almost always true.

```bcl
SET n 7
IF $n % 2 == 0 THEN              # WRONG: equivalent to IF $n (7 is true)
    PUTS "wrong: says 7 is even"
END
IF [CALC $n % 2] == 0 THEN       # RIGHT
    PUTS "even"
ELSE
    PUTS "odd"
END
SET x 15
IF $x > 10 && $x < 20 THEN
    PUTS "between 10 and 20"
END
IF [STREQ 007 7] THEN
    PUTS "equal as text"
ELSEIF 007 == 7 THEN
    PUTS "equal as numbers"
END
IF ! $nothing THEN
    PUTS "a nonexistent variable is false"
END
```
```text
wrong: says 7 is even
odd
between 10 and 20
equal as numbers
a nonexistent variable is false
```

### 7.2 `IF … THEN … ELSEIF … ELSE … END`

`ELSEIF` and `ELSE` are optional (there may be several `ELSEIF`); `END` is mandatory.
Blocks nest freely.

```bcl
PROC grade WITH mark DO
    IF $mark >= 9 THEN
        RETURN "Outstanding"
    ELSEIF $mark >= 7 THEN
        RETURN "Good"
    ELSEIF $mark >= 5 THEN
        RETURN "Pass"
    ELSE
        RETURN "Fail"
    END
END
PUTS [grade 9.5]
PUTS [grade 6]
PUTS [grade 3]
SET year 2024
IF [CALC $year % 4 == 0 && ($year % 100 != 0 || $year % 400 == 0)] THEN
    PUTS "$year is a leap year"
END
```
```text
Outstanding
Pass
Fail
2024 is a leap year
```

> **Known limitation (build 7).** Inside a parenthesised group of an `IF` condition, if
> the first operand already decides the result (`(0 && x)`, `(1 || x)`), the kernel does
> not parse the rest and gives `?EVAL-F-UNTERMCOND, Unterminated condition group`. Avoid
> it by moving compound logic into a single `[CALC …]`, as in the previous example.

### 7.3 The `IF … DO … LOOP` loop

It runs while the condition is true; the condition is evaluated **before** each pass.
The optional `ELSE` runs when the loop ends because the condition became false (also if
it was false from the start) and **is skipped if the loop is left with `BREAK`**.

```bcl
SET i 1
IF $i <= 3 DO
    PUTS "Iteration $i"
    INCR i
ELSE
    PUTS "natural end of the loop"
LOOP
SET count 3
IF $count > 0 DO
    PUTSN "$count "
    INCRV count -1
LOOP
PUTS "Liftoff!"
```
```text
Iteration 1
Iteration 2
Iteration 3
natural end of the loop
3 2 1 Liftoff!
```

### 7.4 `BREAK` and `NEXT`

`BREAK` leaves the innermost loop; `NEXT` jumps to the next iteration. The controlled
infinite loop is `IF 1 DO … BREAK … LOOP`. The `ELSE` lets you detect whether a search
succeeded:

```bcl
SET names "peter ann mary john"
SET i 1
SET total [LSTLENG $names]
IF $i <= $total DO
    SET elem [LSTGET $names $i]
    IF $elem == "ann" THEN
        PUTS "Found at position $i"
        BREAK
    END
    INCR i
ELSE
    PUTS "Not found"
LOOP
SET n 0
IF $n < 10 DO
    INCR n
    IF [CALC $n % 2] == 0 THEN
        NEXT
    END
    PUTSN "$n "
LOOP
PUTS ""
```
```text
Found at position 2
1 3 5 7 9 
```

## 8. Associative arrays

A list stores data **by position**; an array stores them **by name** (key). It is not a
new type: they are ordinary variables named `base(key)`, plus an internal index that
lets you ask about the whole set. Keys and values are strings.

| | List | Array |
|---|---|---|
| Access | by position (1, 2, 3…) | by key |
| Order | preserved | **not guaranteed** |
| Ideal for | sequences, queues | configuration, dictionaries, counters, caches |

### 8.1 Create, read and dynamic keys

No declaration is needed: the first assignment creates it. It is read with
`$name(key)`; the key may come from a variable (resolved first). A nonexistent key gives
`""` without an error.

```bcl
SET city(name) "Madrid"
SET city(country) "Spain"
PUTS $city(name)
PUTS "Capital of $city(country)"
SET field "country"
PUTS $city($field)
PUTS "<$city(missing)>"
```
```text
Madrid
Capital of Spain
Spain
<>
```

Avoid keys with spaces (use `full_name`).

### 8.2 The `ARR*` commands

They receive the array **name**, without `$` and without parentheses: `[ARRLENG config]`.

| Command | Returns |
|---|---|
| `ARREXISTS a` | `1` if it has any element |
| `ARRLENG a` | number of elements |
| `ARRKEYS a` | list of keys (order not guaranteed) |
| `ARRGET a` | flat list `key value key value…` |
| `ARRSET a list` | loads pairs (adds or overwrites; does not delete what was there) |
| `ARRDEL a key` | removes an element (silent if it does not exist) |
| `ARRCLEAR a` | empties the array |

`ARRDEL a k` is equivalent to `UNSET a(k)` and `ARRCLEAR a` to `UNSET a`. `EXISTS a(k)`
asks about a specific key; `ARREXISTS a`, whether there is any element.

```bcl
ARRSET stock "screws 250 nuts 80 washers 1200"
PUTS [ARRLENG stock]
PUTS [EXISTS stock(nuts)]
PUTS [EXISTS stock(nails)]
PUTS [LSTSORT [ARRKEYS stock]]
ARRDEL stock washers
ARRDEL stock missing
PUTS [ARRLENG stock]
SET flat_copy [ARRGET stock]            # serialize
ARRSET other $flat_copy                 # clone
PUTS $other(nuts)
ARRCLEAR stock
PUTS [ARREXISTS stock]
TRY
    ARRSET bad "a b c"
CATCH e
    PUTS $e
END
```
```text
3
1
0
nuts screws washers
2
80
0
?ARRSET-F-BADARGS, List must contain key/value pairs
```

### 8.3 Walking an array

Ask for the keys and walk the list; **sort them** if the output must be reproducible.

```bcl
SET config(host) "localhost"
SET config(port) "8080"
SET keys [LSTSORT [ARRKEYS config]]
SET i 1
IF $i <= [LSTLENG $keys] DO
    SET k [LSTGET $keys $i]
    PUTS "$k = $config($k)"
    INCR i
LOOP
```
```text
host = localhost
port = 8080
```

### 8.4 Vectors and matrices

With consecutive numeric keys (from 1) you get a vector; with compound keys (`i,j`) a
matrix. `ARRLENG` counts the total number of elements.

```bcl
SET v(1) "alpha"
SET v(2) "beta"
SET idx [CALC [ARRLENG v] + 1]
SET v($idx) "gamma"            # append at the end
PUTS "$v(1) $v(2) $v(3) (total [ARRLENG v])"
SET M(1,1) 1
SET M(1,2) 2
SET M(2,1) 3
SET M(2,2) 4
SET i 2
SET j 1
PUTS "M($i,$j) = $M($i,$j); elements: [ARRLENG M]"
```
```text
alpha beta gamma (total 3)
M(2,1) = 3; elements: 4
```

### 8.5 Arrays and `GLOBAL`

Inside a `PROC` variables are local; to work on a global array, link its **base name**
with `GLOBAL`. With arrays the name does not have to exist already. Example: a stack and
a frequency counter.

```bcl
PROC push WITH value DO
    GLOBAL stack
    SET stack([CALC [ARRLENG stack] + 1]) $value
END
PROC pop DO
    GLOBAL stack
    SET idx [ARRLENG stack]
    IF $idx == 0 THEN
        ERROR "The stack is empty"
    END
    SET value $stack($idx)
    ARRDEL stack $idx
    RETURN $value
END
push one
push two
push three
PUTS [pop]
PUTS [pop]
PUTS [ARRLENG stack]

SET words "sun moon sun sea sun moon"
SET n [LSTLENG $words]
SET i 1
IF $i <= $n DO
    SET w [LSTGET $words $i]
    IF [EXISTS count($w)] THEN
        INCR count($w)
    ELSE
        SET count($w) 1
    END
    INCR i
LOOP
SET keys [LSTSORT [ARRKEYS count]]
SET i 1
IF $i <= [LSTLENG $keys] DO
    SET k [LSTGET $keys $i]
    PUTS [STRAPP [STRFMT "%-6s" $k] $count($k)]
    INCR i
LOOP
```
```text
three
two
1
moon  2
sea   1
sun   3
```

`GLOBAL` links **one name per line**; for two global arrays, two lines. After
`GLOBAL stack`, the name stays reserved as an array: a later `SET stack "x"` would give
`?SET-F-DUPLICATE`.

## 9. Lists

A list is a string whose elements are separated by spaces; the nine `LST*` commands
interpret it that way. There is no list type: you can write one by hand, read it from
input or build it with `LSTAPP`.

```bcl
SET colors "red green blue"
PUTS [STRLENG $colors]         # as text: 14 characters
PUTS [LSTLENG $colors]         # as a list: 3 elements
PUTS [LSTGET $colors 2]
```
```text
14
3
green
```

### 9.1 Elements with spaces: braces

An element with spaces is delimited with `{}` (the braces are not part of the element).
`{}` is an **empty** element and it counts. A literal brace is written `\{` or `\}`.

```bcl
SET days {holy monday} tuesday wednesday
PUTS [LSTLENG $days]
PUTS [LSTGET $days 1]
SET v {} alpha beta
PUTS "[LSTLENG $v] elements; the first is <[LSTGET $v 1]>"
SET l one \{two\} three
PUTS [LSTGET $l 2]
PUTS [LSTAPP "{holy monday} tuesday" "{other day}"]    # canonical form
TRY
    PUTS [LSTLENG "{one two"]
CATCH e
    PUTS $e
END
```
```text
3
holy monday
3 elements; the first is <>
{two}
{holy monday} tuesday {other day}
?LIST-F-SYNTAX, Unterminated braced list item
```

These are invalid lists (`?LIST-F-SYNTAX`): an open group that is never closed, text
glued after `}` (`{one}two`) and a stray unescaped brace. Everything the kernel generates
(including `$ARGS`) uses the canonical form with braces.

### 9.2 Measure, read, slice

`LSTGET` out of range returns `""` (position 0 does not exist); `LSTRANG` trims ends that
fall outside and returns `""` if `start > end`.

```bcl
SET week "monday tuesday wednesday thursday friday"
PUTS [LSTLENG ""]
PUTS [LSTLENG "   "]
PUTS "<[LSTGET $week 99]>"
PUTS [LSTRANG $week 1 3]
PUTS [LSTRANG $week 4 99]
PUTS "<[LSTRANG $week 3 1]>"
```
```text
0
0
<>
monday tuesday wednesday
thursday friday
<>
```

### 9.3 Build and modify

**`LSTAPP` concatenates two lists**, it does not «add one element»: a value with spaces
contributes several elements. If it may contain them, wrap it in braces.
`LSTINS list1 list2 pos` inserts *before* `pos` (arguments: original, what is inserted,
position). `LSTDEL list pos count` deletes up to the end if `count` overshoots.

```bcl
SET xs "one two"
SET xs [LSTAPP $xs "three"]
SET xs [LSTAPP $xs "{four five}"]
PUTS "$xs -> [LSTLENG $xs] elements"
SET l "a b c"
PUTS [LSTINS $l "X Y" 2]
PUTS [LSTINS $l "X" 99]
SET l "a b c d e"
PUTS [LSTDEL $l 2 3]
PUTS [LSTDEL $l 4 99]
```
```text
one two three {four five} -> 4 elements
a X Y b c
a b c X
a e
a b c
```

### 9.4 Search and sort

`LSTFIND list elem` returns **all** positions (or `0`), comparing whole elements; it
works as a membership condition. `LSTSORT` sorts numbers by value and then text ignoring
case; only ascending order exists.

```bcl
SET l "red green red blue red"
PUTS [LSTFIND $l "red"]
PUTS [LSTFIND $l "re"]
IF [LSTFIND "read write delete" write] THEN
    PUTS "Action allowed."
END
PUTS [LSTSORT "10 9 2 -3 1.5"]
PUTS [LSTSORT "3 pepe 10 Ann"]
```
```text
1 3 5
0
Action allowed.
-3 1.5 2 9 10
3 10 Ann pepe
```

### 9.5 `LSTFMT`: from text to list

Despite the name, it does **not format**: it *splits* a text on a separator (of one or
more characters). Consecutive separators produce empty elements; an empty separator
leaves the whole text as a single element.

```bcl
SET l [LSTFMT "red,green,blue" ","]
PUTS "$l ([LSTLENG $l])"
PUTS [LSTFMT "a::b::c" "::"]
PUTS [LSTFMT "/usr/local/bin" "/"]
PUTS [LSTLENG [LSTFMT "  hello  world " " "]]
PUTS [LSTLENG "  hello  world "]
```
```text
red green blue (3)
a b c
{} usr local bin
6
2
```

### 9.6 Walking a list and recipes

The central pattern: store the length, a counter from 1 and `LSTGET`.

```bcl
SET colors {dark red} green blue
SET n [LSTLENG $colors]
SET i 1
IF $i <= $n DO
    PUTS "$i -> [LSTGET $colors $i]"
    INCR i
LOOP

PROC lst_join WITH list sep DO
    SET n [LSTLENG $list]
    IF $n == 0 THEN
        RETURN ""
    END
    SET out [LSTGET $list 1]
    SET i 2
    IF $i <= $n DO
        SET out [STRAPP $out [STRAPP $sep [LSTGET $list $i]]]
        INCR i
    LOOP
    RETURN $out
END
PUTS [lst_join "red green blue" ", "]
```
```text
1 -> dark red
2 -> green
3 -> blue
red, green, blue
```

## 10. Program arguments

Everything written after the script name ends up in the reserved variable `$ARGS`, a
**canonical list** (always defined; empty if there are no arguments). The script name is
**not** part of it: the first argument is number 1. Being a list, empty arguments, ones
with spaces and ones with braces are preserved.

```bash
bcl args.bcl alpha "" "two three" "with{brace}" -v
```

```bcl
PUTS "Received: [LSTLENG $ARGS]"
PUTS "ARGS = $ARGS"
SET n [LSTLENG $ARGS]
SET i 1
IF $i <= $n DO
    PUTS "$i -> <[LSTGET $ARGS $i]>"
    INCR i
LOOP
```
```text
Received: 5
ARGS = alpha {} {two three} {with\{brace\}} -v
1 -> <alpha>
2 -> <>
3 -> <two three>
4 -> <with{brace}>
5 -> <-v>
```

Always read with `LSTLENG` and `LSTGET`; splitting `$ARGS` by hand with `STRFIND` or
`LSTFMT` breaks the protections the interpreter has already put in place.

### 10.1 Validate and finish with `EXIT`

`EXIT code` ends the **whole** program (from inside a procedure or a loop too) and
returns the code to the system. Convention: `0` success; `1` generic error (also that of
an uncaught error); other values with the meaning you document. A program that reaches
the end without `EXIT` returns 0.

```bcl
# Usage: sum.bcl <number1> <number2>
IF [LSTLENG $ARGS] != 2 THEN
    PUTS "Usage: sum.bcl <number1> <number2>"
    EXIT 2
END
PUTS [CALC [LSTGET $ARGS 1] + [LSTGET $ARGS 2]]
```
```text
Usage: sum.bcl <number1> <number2>
(exit status: 2)
```

With `7 5` it prints `12` and ends with 0. To check that the data are numbers, wrap
`CALC` in `TRY` (section 14): `CALC` raises `?CALC-F-UNKNOWN` on non-numeric text.

> **Known defect (build 7).** If any error has previously been caught with `TRY`/`CATCH`,
> a later `EXIT n` with `n` other than 0 ends with code 1 and prints the already-caught
> error again on `stderr`, instead of exiting with `n`. `EXIT 0`, and `EXIT n` with no
> previously caught errors, work fine. Until it is fixed, do not rely on exit codes other
> than 1 in programs that catch errors.

### 10.2 Parsing options

The pattern is to walk `$ARGS` deciding what each element is; an option with a value
consumes an extra argument with another `INCR i`. The brackets of the usage message are
written `\[` and `\]`.

```bcl
SET verbose 0
SET times 1
SET text ""
SET n [LSTLENG $ARGS]
SET i 1
IF $i <= $n DO
    SET a [LSTGET $ARGS $i]
    IF [STREQ $a "-v"] THEN
        SET verbose 1
    ELSEIF [STREQ $a "-n"] THEN
        INCR i
        SET times [LSTGET $ARGS $i]
    ELSEIF [STREQ $a "--help"] THEN
        PUTS "Usage: options.bcl \[-v\] \[-n TIMES\] TEXT"
        EXIT 0
    ELSE
        SET text $a
    END
    INCR i
LOOP
IF $verbose THEN
    PUTS "Repeating '$text' $times times"
END
SET k 1
IF $k <= $times DO
    PUTS $text
    INCR k
LOOP
```
```text
Repeating 'hello' 3 times
hello
hello
hello
```

### 10.3 Executable scripts

A first line `#!/usr/bin/env bcl` is a comment to BCL and, to the operating system, the
interpreter to use. With `chmod +x` the script is invoked like any other command.
Remember that the shell expands `*`, `$` and quotes before BCL sees the arguments.

## 11. Modules, `SOURCE` and the startup environment

### 11.1 `SOURCE` and `SOURCEDIR`

`SOURCE "path.bcl"` runs another file **in the current scope and at that moment**: all its
top-level code runs, and its procedures and variables become available. If it fails, the
diagnostic keeps the child file, the line and the chain of inclusions.

```bcl
# utils.bcl: utility library
PROC title WITH text DO
    PUTS "== $text =="
END
SET utils_version 2
```

```bcl
SOURCE "utils.bcl"
title "My program"
PUTS "utils v$utils_version"
```
```text
== My program ==
utils v2
```

Relative `SOURCE` paths are resolved against the **working directory**, not against the
file containing the statement. For the program to work wherever it is launched from, use
`SOURCEDIR`, which returns the directory of the file being evaluated (`.` if the name has
no directory):

```bcl
SOURCE [STRAPP [SOURCEDIR] "/utils.bcl"]
title "Robust path"
TRY
    SOURCE [STRAPP [SOURCEDIR] "/optional.bcl"]
CATCH e
    PUTS "Warning: $e_INFO(CODE)"
END
```
```text
== Robust path ==
Warning: NOFILE
```

### 11.2 Binary modules: `LOAD`, `UNLOAD` and `BCL_LIBS`

A `.bll` module is a dynamic library that adds commands to the interpreter. **Modules are
not part of the kernel and are distributed separately**; this manual documents none of
them. What the kernel provides is:

- `LOAD path.bll` loads the module; its commands are available at once. It returns a
  status line `LOAD OK module=<resolved path> version=<version>` (`unknown` if the module
  declares no version). `UNLOAD` unloads it and removes its commands.
- A name **with `/`** is used as is. A name **without `/`** is searched, in priority
  order, in the directories of the variable `BCL_LIBS` (comma-separated; spaces and empty
  entries are ignored; an entry `~/` expands with `$HOME`). The first directory that
  contains the file wins. If `BCL_LIBS` is defined and the module is not found,
  `?LOAD-F-NOTFOUND` lists where it looked; without `BCL_LIBS` there is no search (the
  name is passed to the system loader). Initial value: the environment variable
  `BCL_LIBS`.
- If two modules define the same command, the last one loaded wins; on `UNLOAD`, the
  command goes back to the kernel built-in.
- A module may publish its own commands, including event commands (`INT`, `TIMER`,
  `RUN`…).

To write a module in C see `doc/bcl_external_bll_interface_v1_1.md`.

### 11.3 The startup file `$HOME/.bcl.rc`

At start-up the interpreter runs `$HOME/.bcl.rc` (if it exists) **before any other
statement**: before the script, its `SOURCE`s and the interactive session, in the global
scope (what it defines remains available). `$ARGS` is already defined. It is used to set
`BCL_LIBS`, common procedures or modules that should always be loaded.

| Situation | What happens |
|---|---|
| The file does not exist | Nothing: start-up continues |
| Error in the file | The error is shown and the interpreter ends with code 1 without running the script |
| `EXIT n` in the file | The interpreter ends with that code, without running the script |
| `bcl --no-rc` or `BCL_NO_RC` | Not read |
| `bcl --restricted` / `BCL_RESTRICTED=1` | Not read |
| No `HOME` | Not read |

```bash
$ cat ~/.bcl.rc
PUTS rc-ran
PUTS "ARGS=$ARGS"
$ bcl main.bcl a b            # main.bcl contains: PUTS main
rc-ran
ARGS=a b
main
$ bcl --no-rc main.bcl
main
```

A portable script must not depend on each user's startup file: test it with
`bcl --no-rc`. (If the `.bcl.rc` does a `LOAD` of a module and the script repeats it, it
fails with a module-already-loaded error.)

### 11.4 Version and restricted mode

`bcl --version` prints `BCL-32 v1.2 build N`. In **restricted mode** (`--restricted` or
`BCL_RESTRICTED=1`) `LOAD`/`UNLOAD` are disabled (`?LOAD-F-RESTRICTED`) and `SOURCE` can
only load files inside the root set with `--source-root` or `BCL_SOURCE_ROOT` (otherwise
`?SRC-F-RESTRICTED`). Use it when the script comes from an untrusted source.

## 12. Procedures and scope

A procedure is a named block that is invoked like any other command (to BCL there is no
difference between `PUTS` and a `PROC` of your own).

```text
PROC name DO
    statements
END

PROC name WITH required… @optional… DO
    statements
    RETURN value
END
```

It must be defined **before** it is called (the file is read top to bottom; otherwise
`?EVAL-F-UNKCMD`). Recommended structure: `LOAD`/`SOURCE`, then all the `PROC`s, and the
main program last. Redefining is legal: the last definition wins.

### 12.1 Parameters

Arguments are assigned **by position**; the count is checked on every call and the
message gives the range (required..required+optional). A parameter with `@` is optional;
all the following ones must be optional too (otherwise `?PAR-F-PROCDEF` at definition
time). An omitted optional is **not created**: tell it apart from «passed empty» with
`EXISTS`.

```bcl
PROC rectangle WITH width height DO
    PUTS [STRFMT "Area: %g" [CALC $width * $height]]
END
rectangle 5 3

PROC connect WITH host @port @timeout DO
    SET p 8080                       # default values
    SET t 30
    IF [EXISTS port] THEN
        SET p $port
    END
    IF [EXISTS timeout] THEN
        SET t $timeout
    END
    PUTS "$host port=$p timeout=$t"
END
connect "srv.local"
connect "srv.local" 9000
connect "srv.local" 9000 5

PROC empty_or_not WITH a @b DO
    IF [EXISTS b] THEN
        PUTS "b passed, value <$b>"
    ELSE
        PUTS "b not passed"
    END
END
empty_or_not 1
empty_or_not 1 ""
TRY
    rectangle 1
CATCH e
    PUTS $e
END
```
```text
Area: 15
srv.local port=8080 timeout=30
srv.local port=9000 timeout=30
srv.local port=9000 timeout=5
b not passed
b passed, value <>
?EVAL-F-BADARGS, rectangle: expected between 2 and 2 arguments, got 1
```

### 12.2 `RETURN`

`RETURN value` ends the procedure and returns the value; without `RETURN` (or with a bare
`RETURN`) it returns `""`. It serves for early exits («guard clauses»). Since everything
is a string, lists can be returned; for an array, return `[ARRGET a]` or use `GLOBAL`.
Without brackets, the returned value is discarded.

```bcl
PROC add WITH a b DO
    RETURN [CALC $a + $b]
END
PUTS [add 100 [add 1 2]]

PROC only_prints DO
    PUTS "I do something"
END
SET r [only_prints]
PUTS "result=<$r>"
```
```text
103
I do something
result=<>
```

### 12.3 Variable scope

Each procedure has its **own scope**: what it creates inside stays inside, and it **does
not see** global variables (reading them gives `""` without an error, which goes
unnoticed). Passing data as parameters is preferable.

```bcl
SET heading "Report"
SET message "I am outside"
PROC test DO
    SET message "I am inside"
    PUTS "inside: $message, heading=<$heading>"
END
test
PUTS "outside: $message"
```
```text
inside: I am inside, heading=<>
outside: I am outside
```

`GLOBAL name` links **one** name to the global variable of the same name (two names on a
line give `?EVAL-F-BADARGS`). For a scalar, **create it beforehand** in the global scope:
if it does not exist, `GLOBAL` treats it as an array and a later scalar `SET` fails with
`?SET-F-DUPLICATE`.

```bcl
SET counter 0
PROC increment DO
    GLOBAL counter
    INCR counter
END
increment
increment
PUTS $counter

PROC bad DO
    GLOBAL fresh
    SET fresh 5
END
TRY
    bad
CATCH e
    PUTS $e
END
```
```text
2
?SET-F-DUPLICATE, Variable 'fresh' already exists as array
```

Parameters are protected: `UNSET` of an active parameter gives
`?UNSET-F-BADVALUE, Cannot remove active parameter 'n'`; copy the value to another
variable.

### 12.4 Recursion

Every recursive procedure needs a base case and must move towards it. Without them, the
kernel cuts off at the nesting limit with `?EVAL-F-DEPTH` instead of exhausting the stack.

```bcl
PROC factorial WITH n DO
    IF $n <= 1 THEN
        RETURN 1
    END
    RETURN [CALC $n * [factorial [CALC $n - 1]]]
END
PUTS [factorial 5]
PUTS [factorial 10]

PROC endless WITH n DO
    RETURN [endless [CALC $n + 1]]
END
TRY
    endless 1
CATCH e
    PUTS $e
END
```
```text
120
3.6288e+06
?EVAL-F-DEPTH, Subcommand nesting exceeds limit
```

Remember section 4.2: `[factorial 10]` is shown as `3.6288e+06` (6 significant
digits), not as 3628800.

Good design: one responsibility per procedure, parameters rather than `GLOBAL`, return
values instead of printing them, and group reusable code in a file for `SOURCE`.

## 13. Strings

Five rules: (1) positions start at 1; (2) counting is by **character**, not by byte
(UTF-8); (3) **no command modifies the original string**: they return a new one (without
`SET` and brackets the result is lost); (4) `0` means «no result»; (5) asking for
something outside the string is not an error: it gives `""` and ranges are trimmed.

### 13.1 Measure, compare and extract

`STREQ` compares text strictly (case and spaces count, `007` ≠ `7`). To ignore case,
normalise first with `STRLOW`. `STRRANG` includes both ends (returns `end - start + 1`
characters).

```bcl
SET text "Hello world"
PUTS [STRLENG "canción"]
PUTS [STREQ "hello" "HELLO"]
PUTS [STREQ [STRLOW "YES"] "yes"]
PUTS [STRGET $text 1]
PUTS "<[STRGET $text 99]>"
PUTS [STRRANG $text 1 5]
PUTS [STRRANG $text 7 [STRLENG $text]]
PUTS [STRRANG "Hello" 3 99]
PUTS [STRRANG "Hello" 0 4]
SET nothing [STRRANG "Hello" 7 3]
PUTS "<$nothing>"
```
```text
7
0
1
H
<>
Hello
world
llo
Hell
<>
```

### 13.2 Join, insert and delete

`STRAPP` joins **exactly two** strings (for three, nest). `STRINS str text pos`: the order
is string, text, position. `STRDEL str pos count`. There is no «replace»: it is built
with `STRDEL` + `STRINS`.

```bcl
PUTS [STRAPP "20" "26"]
PUTS [STRAPP "Ada" [STRAPP " " "Lovelace"]]
PUTS [STRINS "Hllo" "e" 2]
PUTS [STRINS "abc" "X" 0]
PUTS [STRINS "abc" "X" 99]
PUTS [STRDEL "Hello world" 6 1]
PUTS [STRDEL "abc" 2 99]
SET s "Hello world"
SET s [STRDEL $s 7 5]
SET s [STRINS $s "BCL" 7]
PUTS $s
```
```text
2026
Ada Lovelace
Hello
Xabc
abcX
Helloworld
a
Hello BCL
```

### 13.3 Search

`STRFIND str sub` returns **all** positions (overlapping ones too) as a list, or `0`. It
works as a condition; but do not compare with `== 3`: there may be several.

```bcl
PUTS [STRFIND "bananana" "na"]
PUTS [STRFIND "aaa" "aa"]
PUTS [STRFIND "hello" "zz"]
SET line "ERROR: disk full"
IF [STRFIND $line "ERROR"] THEN
    PUTS "The line reports an error."
END
SET pos [STRFIND "the cat and the dog and the fish" "the"]
PUTS "[LSTLENG $pos] occurrences; the first at [LSTGET $pos 1]"
```
```text
3 5 7
1 2
0
The line reports an error.
3 occurrences; the first at 1
```

### 13.4 Transform

`STRUPP` and `STRLOW` convert ASCII only (`ó` and `Ñ` do not change); `STRREV` respects
UTF-8.

```bcl
PUTS [STRUPP "Hello"]
PUTS [STRLOW "Hello"]
PUTS [STRREV "Hello"]
PUTS [STRUPP "canción"]
PUTS [STRREV "canción"]
```
```text
HELLO
hello
olleH
CANCIóN
nóicnac
```

### 13.5 `STRFMT`

`STRFMT "template" [value]`: text with **a single** specifier
`%[flags][width][.precision]type` (types and flags in section 2.4). The width is a
minimum (pads on the left; `-` left-aligns), the precision is the number of decimals (or
the maximum number of characters in `%s`).

```bcl
PUTS [STRFMT "Code: %05d" 42]
PUTS [STRFMT "User %s connected" "ann"]
PUTS [STRFMT "|%8s|" "ab"]
PUTS [STRFMT "|%-8s|" "ab"]
PUTS [STRFMT "%.2f" 3.14159]
PUTS [STRFMT "%.3s" "abcdefg"]
PUTS [STRFMT "%08.2f" 3.5]
PUTS [STRFMT "%+d" 7]
PUTS [STRFMT "%#x" 255]
PUTS [STRFMT "%X" 255]
PUTS [STRFMT "%o" 8]
PUTS [STRFMT "%e" 12345.7]
PUTS [STRFMT "%c" 65]
PUTS [STRFMT "100%% sure"]
PUTS [STRFMT "%d" 3.9]
PUTS [STRFMT "%d" "hello"]
```
```text
Code: 00042
User ann connected
|      ab|
|ab      |
3.14
abc
00003.50
+7
0xff
FF
10
1.234570e+04
A
100% sure
3
0
```

A template with **two** slots is an error (it is rejected before being applied). For
several values, chain calls with `STRAPP`:

```bcl
PROC row WITH name units price DO
    SET s [STRFMT "%-12s" $name]
    SET s [STRAPP $s [STRFMT "%5d" $units]]
    RETURN [STRAPP $s [STRFMT "%10.2f" $price]]
END
PUTS [row "Screws" 250 0.03]
PUTS [row "Washers" 1200 0.01]
```
```text
Screws        250      0.03
Washers      1200      0.01
```

`STRFMT` validates the template before applying it (no `%n`, `*` or `%ld`; width and
precision ≤ 4096). A value that does not fit is not an error: `%d` with `hello` gives `0`
and with `3.9` gives `3` (truncates).

Rejected: (`?STRFMT-F-BADARGS`) two or more specifiers, a literal with a value and more
than two arguments; (`?STRFMT-F-UNSUPPORTED`) unknown types (`%y`), `%n`, `*`, `%ld`;
(`?STRFMT-F-BADVALUE`) width or precision > 4096.

```bcl
TRY
    PUTS [STRFMT "%s has %d" "ann" 3]
CATCH e
    PUTS $e
END
TRY
    PUTS [STRFMT "%y" 1]
CATCH e
    PUTS $e
END
```
```text
?STRFMT-F-BADARGS, Expected 1 or 2 arguments, got 3
?STRFMT-F-UNSUPPORTED, Unsupported conversion specifier 'y'
```

### 13.6 Recipes

```bcl
PROC replace WITH text old new DO      # first occurrence
    SET pos [STRFIND $text $old]
    IF $pos == 0 THEN
        RETURN $text
    END
    SET p [LSTGET $pos 1]
    RETURN [STRINS [STRDEL $text $p [STRLENG $old]] $new $p]
END
PROC trim WITH s DO
    SET first 1
    SET last [STRLENG $s]
    IF $first <= $last && [STREQ [STRGET $s $first] " "] DO
        INCR first
    LOOP
    IF $last >= $first && [STREQ [STRGET $s $last] " "] DO
        INCRV last -1
    LOOP
    RETURN [STRRANG $s $first $last]
END
PUTS [replace "Hello world" "world" "BCL"]
SET clean [trim "   hello   "]
PUTS "<$clean>"
```
```text
Hello BCL
<hello>
```

Do not nest double quotes inside double quotes (`"[trim "  x "]"` closes the outer
string): store the value in a variable first, as above.

## 14. Error handling

If an error is not caught, the interpreter prints the diagnostic on `stderr`
(`Error: file:line:col: ?FAC-SEV-CODE, text`, plus `In:` and `Trace:` where applicable)
and ends with code 1. Typical cases: unknown command, wrong number of arguments, `CALC`
on text, invalid `STRFMT` template, unbalanced braces, an unclosed block, your own
`ERROR`, `SOURCE`/`LOAD` of a nonexistent file.

**These are not errors:** reading a nonexistent variable (gives `""`), asking for a
character or element out of range (gives `""`), deleting something that does not exist.
Division and modulo by zero in `CALC` **are** errors.

### 14.1 `TRY … CATCH var … END`

If something fails inside `TRY`, execution jumps to `CATCH`; `var` receives the readable
text of the diagnostic. What follows the failure inside `TRY` does not run.

```bcl
TRY
    PUTS "Before the error"
    ERROR "controlled failure"
    PUTS "This does not run"
CATCH err
    PUTS "Caught: $err"
END
PUTS "The program goes on"
```
```text
Before the error
Caught: ?EVAL-F-USERERR, controlled failure
The program goes on
```

### 14.2 `ERROR` and propagation

`ERROR "message"` raises a `?EVAL-F-USERERR` error. An error not caught inside a
procedure goes up the call stack to the first `TRY` that catches it.

```bcl
PROC divide WITH a b DO
    IF $b == 0 THEN
        ERROR "Cannot divide by zero"
    END
    RETURN [CALC $a / $b]
END
PROC level2 DO
    PUTS [divide 8 0]          # does not catch: it propagates
END
TRY
    PUTS [divide 10 4]
    level2
CATCH e
    PUTS "Caught above: $e"
END
```
```text
2.5
Caught above: ?EVAL-F-USERERR, Cannot divide by zero
```

### 14.3 Structured fields: `var_INFO(...)`

Besides the text, the kernel creates the array `var_INFO` (so it does not clash with the
scalar `var`). Fields: `TEXT` `SUMMARY` `FACILITY` `SEVERITY` `CODE` `FILE` `LINE` `COL`
`PROC` (active procedure) `EVENT` `TRACE` `OSCODE` `OSMSG` `DISPLAY`.

```bcl
PROC p DO
    ERROR "example failure"
END
TRY
    p
CATCH err
    PUTS $err
    PUTS "$err_INFO(FACILITY) $err_INFO(SEVERITY) $err_INFO(CODE)"
    PUTS "$err_INFO(FILE) line $err_INFO(LINE) col $err_INFO(COL)"
    PUTS "proc=$err_INFO(PROC) summary=$err_INFO(SUMMARY)"
END
```
```text
?EVAL-F-USERERR, example failure
EVAL F USERERR
example.bcl line 2 col 11
proc=p summary=example failure
```

### 14.4 Patterns

Validate input with `TRY` + `CALC`; catch where you can do something useful; do not use
`TRY` for normal flow; do not leave an empty `CATCH`. Be specific in messages
(`ERROR "The radius cannot be negative: $r"`).

```bcl
SET values "-5 16 1500"
SET i 1
IF $i <= 3 DO
    SET v [LSTGET $values $i]
    TRY
        IF $v < 0 THEN
            ERROR "negative value: $v"
        END
        SET r [STRFMT "%.4f" [CALC sqrt($v)]]
        PUTS "sqrt($v) = $r"
    CATCH e
        PUTS "warning: $e"
    END
    INCR i
LOOP
```
```text
warning: ?EVAL-F-USERERR, negative value: -5
sqrt(16) = 4.0000
sqrt(1500) = 38.7298
```

The diagnostic format (`?FAC-SEV-CODE`) is guaranteed for the kernel's native commands; a
module may use its own messages. Remember the defect of `EXIT n` after a caught error
(section 10.1).

## 15. Exercises and idioms

Complete programs, run as shown.

### 15.1 FizzBuzz

```bcl
SET i 1
IF $i <= 15 DO
    IF [CALC $i % 15] == 0 THEN
        PUTS "FizzBuzz"
    ELSEIF [CALC $i % 3] == 0 THEN
        PUTS "Fizz"
    ELSEIF [CALC $i % 5] == 0 THEN
        PUTS "Buzz"
    ELSE
        PUTS $i
    END
    INCR i
LOOP
```
```text
1
2
Fizz
4
Buzz
Fizz
7
8
Fizz
Buzz
11
Fizz
13
14
FizzBuzz
```

### 15.2 Greatest common divisor and prime numbers

```bcl
PROC gcd WITH a b DO
    IF $b == 0 THEN
        RETURN $a
    END
    RETURN [gcd $b [CALC $a % $b]]
END
PROC is_prime WITH n DO
    IF $n < 2 THEN
        RETURN 0
    END
    SET d 2
    IF [CALC $d * $d] <= $n DO
        IF [CALC $n % $d] == 0 THEN
            RETURN 0
        END
        INCR d
    LOOP
    RETURN 1
END
PUTS [gcd 48 18]
SET primes ""
SET n 2
IF $n <= 30 DO
    IF [is_prime $n] THEN
        SET primes [LSTAPP $primes $n]
    END
    INCR n
LOOP
PUTS $primes
```
```text
6
2 3 5 7 11 13 17 19 23 29
```

### 15.3 Palindrome with two indices

```bcl
PROC palindrome WITH s DO
    SET i 1
    SET j [STRLENG $s]
    IF $i < $j DO
        IF [STREQ [STRGET $s $i] [STRGET $s $j]] == 0 THEN
            RETURN 0
        END
        INCR i
        INCRV j -1
    LOOP
    RETURN 1
END
PUTS [palindrome "racecar"]
PUTS [palindrome "hello"]
```
```text
1
0
```

### 15.4 Reference idioms

| Need | Idiom |
|---|---|
| Counter | `INCR i` / `INCRV total $x` |
| Accumulate text | `SET s [STRAPP $s "x"]` starting from `SET s ""` |
| Append to a list | `SET l [LSTAPP $l "{$elem}"]` (wrapped if it may contain spaces) |
| Controlled infinite loop | `IF 1 DO … BREAK … LOOP` |
| Search with «not found» | loop with `BREAK` and an `ELSE` block |
| Set membership | `SET set(x) 1` and `EXISTS set(x)` |
| Default value | `SET p 8080` and, if `[EXISTS port]`, `SET p $port` (section 12.1) |
| Condition with arithmetic | `IF [CALC $n % 2] == 0 THEN` |
| Exact text comparison | `IF [STREQ $a $b] THEN` |
| Descending order | `LSTSORT` and walk from the end to the start |
| Leave on end of input | `BREAK` if `[STREQ $d ""]` after each `GETS` (section 6.2) |
| Portability | test with `bcl --no-rc` |

## Appendix: known kernel defects (build 7)

Verified while preparing this manual; keep them in mind until they are fixed.

1. **`EXIT n` (n ≠ 0) after a caught error** ends with code 1 and prints the
   already-caught error again (section 10.1).
2. **Short-circuited groups in `IF`**: `IF (0 && x) …` or `IF (1 || x) …` give
   `?EVAL-F-UNTERMCOND` (section 7.2).
3. **Precision**: `CALC`, `INCR` and `INCRV` write 6 significant digits (section 4.2).
4. **`-3 ** 2`** is evaluated as `(-3) ** 2` with an imaginary residue (section 4.1).

## Appendix: Windows notes

This manual is shared by the Linux and Windows cores. On Windows:

- The startup file is `%USERPROFILE%\.bcl.rc` (where this manual says `$HOME/.bcl.rc`).
- In `BCL_LIBS`, directories are separated by commas as everywhere (`C:\bcl\lib,D:\mods`);
  a name containing `/` or `\` is used as written.
- `CALC` integer operations are 64-bit on every platform.
- Native modules (`.bll`) are not available yet, so the module commands described in
  section 11 only matter for a future release.
- The examples that read arguments or paths work unchanged; use `bcl.exe script.bcl`
  instead of `./bin/bcl script.bcl`.
