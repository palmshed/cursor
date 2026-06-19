# Cursor

[![CI](https://github.com/bniladridas/cursor/actions/workflows/ci.yml/badge.svg)](https://github.com/bniladridas/cursor/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/bniladridas/cursor?label=release&logo=github)](https://github.com/bniladridas/cursor/releases)
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
brew tap palmshed/cursor
brew install palmshed/cursor/cursor --formula
```

**Curl** (binary: `cursor-agent`):
```bash
curl -fsSL https://github.com/bniladridas/cursor/raw/main/install.sh | sudo sh
```

To install to a writable directory without `sudo`:
```bash
curl -fsSL https://github.com/bniladridas/cursor/raw/main/install.sh | INSTALL_DIR=~/.local/bin sh
```

**From source** (binary: `cursor-agent`):
```bash
cmake -S . -B build && cmake --build build
./build/cursor-tests   # run tests
```

## Platforms

Pre-built binaries for Linux (amd64), macOS (arm64), and Windows (amd64) are available on the [releases page](https://github.com/bniladridas/cursor/releases).

[![Cursor](.github/packaging/brand-cursor.png)](https://github.com/bniladridas/cursor/releases)