#!/bin/bash
# Cursor Agent - Binary Installation Script

set -e

INSTALL_DIR="/usr/local/bin"
CONFIG_DIR="$HOME/.cursor"
VERSION="0.1"

echo "Installing Cursor v${VERSION}..."

# Detect platform
[[ "$OSTYPE" == "darwin"* ]] && PLATFORM="macos" || PLATFORM="linux"

# Install
mkdir -p "$CONFIG_DIR"
sudo cp "cursor-agent-${PLATFORM}" "$INSTALL_DIR/cursor-agent" && sudo chmod +x "$INSTALL_DIR/cursor-agent" || { echo "Binary not found"; exit 1; }

# Config
[ -f "$CONFIG_DIR/.env" ] || cp .env.example "$CONFIG_DIR/.env"
mkdir -p "$CONFIG_DIR/data"

echo "Installed. Edit $CONFIG_DIR/.env and run: cursor-agent"
