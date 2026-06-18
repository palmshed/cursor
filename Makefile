.PHONY: build install clean

PREFIX ?= /usr/local

build:
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
	cmake --build build --config Release

install: build
	install -d $(PREFIX)/bin
	install build/bin/cursor-agent $(PREFIX)/bin/cursor

uninstall:
	rm -f $(PREFIX)/bin/cursor

clean:
	rm -rf build
