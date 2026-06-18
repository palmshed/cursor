.PHONY: build install uninstall clean

PREFIX ?= /usr/local

build:
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
	cmake --build build --config Release

install: build
	@if [ -w "$(PREFIX)/bin" ] 2>/dev/null; then \
		install -d $(PREFIX)/bin; \
		install build/bin/cursor-agent $(PREFIX)/bin/cursor; \
	else \
		echo "Need root access to install to $(PREFIX)/bin"; \
		sudo install -d $(PREFIX)/bin; \
		sudo install build/bin/cursor-agent $(PREFIX)/bin/cursor; \
	fi
	@echo "Installed! Run 'cursor' to start."

uninstall:
	@if [ -w "$(PREFIX)/bin" ] 2>/dev/null; then \
		rm -f $(PREFIX)/bin/cursor; \
	else \
		sudo rm -f $(PREFIX)/bin/cursor; \
	fi

clean:
	rm -rf build
