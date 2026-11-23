# BCL-32 core (Windows)

**BCL-32** is an interpreter for the **Basic Command Language 32** (kernel 1.2): a small,
text-based scripting language. This repository contains the **core for Windows**:
`bcl.exe`, the examples and the language manual. The Linux core lives in
[BCL-32-core-Linux](https://github.com/713avo/BCL-32-core-Linux).

## Download (portable)

Get `bcl-32-<version>-windows-x64.zip` from the
[Releases](../../releases) page, unzip it anywhere and run it — no installer, no
administrator rights:

```bat
bcl.exe --version
bcl.exe examples\01_hola.bcl
bcl.exe                      & rem interactive session
```

Add the folder to your `PATH` if you want to call `bcl` from anywhere.

**Requirements:** Windows 10 or later, 64-bit (the executable uses the Universal C
Runtime that ships with Windows 10; on Windows 7/8.1 install the "Universal C Runtime"
update). No other runtime or DLL is needed.

## The language in a nutshell

- A single data type: **string**. Arithmetic goes through `CALC` (real and complex numbers,
  bases 2/8/10/16, 64-bit integer operations).
- **1-based** indexes. Lists use `{}` for elements that contain spaces. Associative arrays.
- Two control structures: `IF … THEN … END` and the loop `IF … DO … LOOP`
  (with `BREAK`/`NEXT`). Plus procedures, `TRY … CATCH`, `GLOBAL`, `INCR`.

```bcl
PROC factorial WITH n DO
    IF $n <= 1 THEN
        RETURN 1
    END
    RETURN [CALC $n * [factorial [CALC $n - 1]]]
END
PUTS "5! = [factorial 5]"
```

Full documentation: [`doc/manual.md`](doc/manual.md) (English) and
[`doc/manual.es.md`](doc/manual.es.md) (Español). Examples: [`examples/`](examples).

## Windows specifics

- **Startup file:** `%USERPROFILE%\.bcl.rc` is run before any script (`--no-rc` or
  `BCL_NO_RC=1` skip it).
- **Module search path:** the variable `BCL_LIBS` is a comma-separated list of
  directories, e.g. `SET BCL_LIBS "C:\bcl\lib,D:\mods"`.
- **Console:** `bcl.exe` switches the console to UTF-8 and enables ANSI escape
  sequences. Both forward and backward slashes work in paths.
- **Script files** may use LF or CRLF line endings.
- **Native modules (`.bll`) are not supported on Windows yet.** `LOAD` works as a command,
  but there are no Windows modules and the executable does not export the kernel symbols a
  module would need.
- **Interactive session:** uses the standard console line editing (no `readline`).

## Build from source (cross-compiling from Linux)

The Windows executable is cross-compiled with [Zig](https://ziglang.org)'s bundled
mingw-w64 (any `x86_64-w64-mingw32-gcc` works too).

```sh
./tools/install-dev.sh        # downloads Zig into tools/toolchain (checksum verified)
make windows                  # -> bin-win/bcl.exe
make portable                 # -> dist/bcl-32-<version>-windows-x64.zip
```

`./tools/install-dev.sh --wine` (or `--wine-portable`) also installs Wine so you can run
`bcl.exe` on Linux for testing. With another compiler: `make windows CC=x86_64-w64-mingw32-gcc`.
A plain `make` builds the native Linux/macOS binary, which is handy for development.

## Layout

```
src/ include/   the interpreter (include/platform.h and win_complex.h = Windows layer)
examples/       nine small programs
doc/            language manual (English and Spanish)
tools/          install-dev.sh: toolchain (and optional Wine) installer
```

## Status

Development of this repository is closed: it is published as a finished snapshot and does not
accept contributions.

## License

GNU General Public License v3 or later — see [`LICENSE`](LICENSE). Scripts you write in BCL
are not covered by the GPL. The example programs may be freely reused in your own programs.
