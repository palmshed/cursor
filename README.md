# Cursor

Cross-platform AI coding agent. C++20, 16 services, 8 AI providers, CI/CD across 3 platforms.

## Install

Homebrew:
```bash
brew install cursor
```

npm:
```bash
npm i -g @bniladridas/cursor
```

## Build from Source

```bash
cmake -S . -B build && cmake --build build
./build/cursor-tests
```

## Architecture

```
src/
  agent.cpp           # main loop, command routing
  memory_manager.cpp  # persistent conversation store
  services/           # 16 stateless services
  utils/              # config, validation, platform
include/
  agent.h
  services/
  utils/
```

## License

Apache 2.0

![Cursor](.github/packaging/brand-cursor.png)
