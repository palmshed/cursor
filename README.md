# Cursor

Cursor is a terminal-based agent for macOS, Linux, and containerized environments.

It works with codebases, files, commands, and repositories through a CLI interface.

Homebrew:

```bash
brew install cursor
```

npm:

```bash
npm i -g @bniladridas/cursor
```

Build from source:

```bash
cmake -S . -B build
cmake --build build
./build/cursor-tests
```

The project is written in C++20 and uses CMake for building and testing.

Source layout:

```text
src/
  agent.cpp
  memory_manager.cpp
  services/
  utils/

include/
  agent.h
  services/
  utils/
```

License: Apache 2.0

![Cursor](.github/packaging/brand-cursor.png)