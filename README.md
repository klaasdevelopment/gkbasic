# GK-BASIC

A GW-BASIC-inspired interpreter written in portable C11. It provides a classic
`Ok` prompt, editable numbered programs, immediate commands, and a useful subset
of the text-mode BASIC language. It is not a complete or binary-compatible
implementation of Microsoft GW-BASIC.

## Build and run

Requires a C compiler and Make; tests also require Python 3. No external C
libraries are required beyond the standard library and the system math library.

```sh
make                  # build ./gkbasic
make run              # start the interpreter
./gkbasic examples/fibonacci.bas  # run a file and exit
make test             # run regression tests
make clean
```

Use `./gkbasic --quiet` to suppress the banner and `Ok` prompts for piped input.
`./gkbasic --help` prints command-line usage. Execution errors go to stderr and
cause exit status 1; an interactive session remains usable after an error.
Ctrl-C interrupts a running program. EOF or `SYSTEM` exits.

## First program

Enter this at the prompt:

```basic
10 INPUT "Your name";NAME$
20 FOR I=1 TO 3
30 PRINT "Hello, ";NAME$;"!"
40 NEXT I
RUN
SAVE "hello.bas"
```

A numbered line inserts or replaces that program line; entering just its number
removes it. Lines run in numeric order. Statements without numbers execute
immediately. Separate statements with colons. Commands and variable names are
case-insensitive; strings preserve case.

## Supported language

| Area | Commands and features |
| --- | --- |
| Program management | `RUN [line]`, `LIST [first[-last]]`, `NEW`, `CLEAR`, `LOAD "file"`, `SAVE "file"`, `SYSTEM` (`QUIT`/`EXIT`) |
| Variables | `LET A=expression` or `A=expression`; numeric, `$` string, and `%` integer variables |
| Arrays | `DIM A(20),B$(5,5)`; up to four dimensions; zero-based inclusive bounds; implicit bounds of 10 |
| Output | `PRINT` or `?`; semicolon joins output and suppresses a trailing newline; comma prints a tab; `CLS` |
| Input | `INPUT ["prompt";]variable[,variable...]` |
| Branches | `GOTO line`, single-line `IF expression THEN statement-or-line [ELSE statement-or-line]` |
| Loops | `FOR variable=start TO limit [STEP increment]`, `NEXT [variable]`; nested loops supported |
| Subroutines | `GOSUB line`, `RETURN` |
| Data | `DATA value,...`, `READ variable,...`, `RESTORE [line]` |
| Other | `REM`, apostrophe comments, `END`, `STOP`, `RANDOMIZE [seed]` |

Arithmetic supports parentheses, `+`, `-`, `*`, `/`, integer division `\`, `MOD`,
and right-associative `^`. Comparisons are `=`, `<>`, `<`, `<=`, `>`, `>=` and
return -1 for true or 0 for false. `AND`, `OR`, and `NOT` operate on integers.
Strings support concatenation with `+` and lexical comparisons.

Built-in functions:

- Math: `ABS`, `INT`, `FIX`, `SGN`, `SQR`, `SIN`, `COS`, `TAN`, `ATN`, `LOG`, `EXP`.
- Random: `RND` or `RND(x)`; positive x advances, zero repeats the previous value,
  negative x reseeds and generates a value.
- Strings: `LEN`, `VAL`, `ASC`, `CHR$`, `STR$`, `LEFT$`, `RIGHT$`, `MID$`.

Uninitialized numbers are 0 and strings are empty. `RUN`, `NEW`, and `CLEAR`
clear variables, arrays, loop/subroutine stacks, and the DATA cursor. `RUN` keeps
the stored program. `LOAD` validates an ASCII file before replacing the program;
`SAVE` writes numbered ASCII source and overwrites its target file.

Examples include a greeting, Fibonacci numbers, and a number guessing game in
[`examples/`](examples/).

## Compatibility and limits

This implementation focuses on terminal programs. It does not implement graphics,
sound, DOS/hardware commands, tokenized `.BAS` files, file-channel I/O, `DEF FN`,
`ON ERROR`, `WHILE/WEND`, or `CONT`. Use separate lines for nested `IF` statements;
nested single-line `IF/ELSE` and comma-separated `NEXT` lists are not supported.
`GOTO`, `GOSUB`, `FOR`, and `NEXT` require a running numbered program. `LOAD` and
`NEW` are direct-mode commands.

Numeric values use C doubles, not Microsoft's original floating-point formats.
`%` values and integer operators truncate to the host C `int` range. Numeric
printing uses compact formatting without GW-BASIC's surrounding spaces, comma
separators use terminal tabs, and `STR$` omits a leading positive-number space.
`INPUT` reads one line per variable, including when several variables are listed.
Strings cannot contain NUL bytes; `CHR$(0)` is rejected. Random sequences are
host-dependent.

Limits: 10,000 program lines numbered 1–65529; input lines and strings up to
4,095 bytes (including the line number for input); 2,048 scalar variables and
accessed array elements combined; 128 arrays; four dimensions with bounds up to
32,767; 64 levels of expression nesting; and 256 nested loop/subroutine frames each. Array storage is sparse.
