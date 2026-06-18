# Cursor

Cross-platform AI coding agent. C++20, 16 services, 8 AI providers, CI/CD across 3 platforms.

## Quick Start

```bash
brew install cpr nlohmann-json cmake
cmake -S . -B build && cmake --build build
./build/cursor-agent
```

## Build & Test

macOS: `brew install cpr nlohmann-json cmake`  
Linux: `apt install nlohmann-json3-dev libcurl4-openssl-dev cmake`

```bash
cmake -S . -B build && cmake --build build
./build/cursor-tests
```

## Docker

```bash
docker build -t cursor -f .github/packaging/docker/Dockerfile .
docker run -it cursor
```

## License

Apache 2.0
