# Cursor

[![CI](https://github.com/bniladridas/cursor/actions/workflows/ci.yml/badge.svg)](https://github.com/bniladridas/cursor/actions/workflows/ci.yml)
[![Release](https://github.com/bniladridas/cursor/actions/workflows/release.yml/badge.svg)](https://github.com/bniladridas/cursor/actions/workflows/release.yml)
[![npm](https://img.shields.io/npm/v/@bniladridas/cursor)](https://www.npmjs.com/package/@bniladridas/cursor)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue)](LICENSE)

Cursor is a cross-platform AI coding agent that works with codebases, files, commands, and repositories through a CLI interface.

## Install

**npm** (binary: `cursor`):
```bash
npm i -g @bniladridas/cursor
```

**Homebrew** (binary: `cursor-agent`):
```bash
brew tap bniladridas/cursor https://github.com/bniladridas/cursor
brew install cursor
```

**From source** (binary: `cursor`):
```bash
cmake -S . -B build && cmake --build build
./build/cursor-tests   # run tests
```

## Platforms

Pre-built binaries for Linux (amd64), macOS (arm64), and Windows (amd64) are available on the [releases page](https://github.com/bniladridas/cursor/releases).

[![Cursor](.github/packaging/brand-cursor.png)](https://github.com/bniladridas/cursor/releases)