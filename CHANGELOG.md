# Changelog

Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added
- First Windows release: `bcl.exe` (x64) cross-compiled from Linux, portable ZIP, language
  manual (English and Spanish) and examples.
- Platform layer (`include/platform.h`, `include/win_complex.h`): dynamic loading through
  `LoadLibrary`, `%USERPROFILE%` for the startup file, UTF-8 console with ANSI sequences,
  and portable complex-math functions (the Windows C runtime versions use another ABI and
  are missing in some environments).

### Changed
- `CALC` integers are now always 64-bit (`long` is 32-bit on Windows): shifts, bitwise
  operations and hexadecimal/octal literals behave the same on every platform.
- Script files are read in binary mode (LF and CRLF line endings both work).

## [1.2] — 2026-11

### Added
- `INCR var [delta]`: the increment may be negative or decimal.
- System variable `BCL_LIBS`; startup file `.bcl.rc`; options `--no-rc` and `--version`;
  every version has a build number.

### Changed
- License: GPL-3.0-or-later.
