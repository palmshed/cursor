#!/usr/bin/env bash
set -euo pipefail

REPO="bniladridas/cursor"
INSTALL_DIR="${INSTALL_DIR:-/usr/local/bin}"
VERSION="${VERSION:-}"

if [ -z "$VERSION" ]; then
  VERSION=$(curl -fsSL "https://api.github.com/repos/$REPO/releases/latest" | grep '"tag_name"' | cut -d'"' -f4)
fi
VERSION="${VERSION#v}"

case "$(uname -s)" in
  Darwin)  OS="darwin" ;;
  Linux)   OS="linux" ;;
  *)       echo "unsupported OS: $(uname -s)"; exit 1 ;;
esac

case "$(uname -m)" in
  arm64|aarch64) ARCH="arm64" ;;
  x86_64|amd64)  ARCH="amd64" ;;
  *)             echo "unsupported arch: $(uname -m)"; exit 1 ;;
esac

if [ "$OS" = "darwin" ] && [ "$ARCH" = "arm64" ]; then
  ARCHIVE="cursor_v${VERSION}_darwin_arm64.tar.gz"
  BINARY="cursor-macos"
elif [ "$OS" = "darwin" ] && [ "$ARCH" = "amd64" ]; then
  ARCHIVE="cursor_v${VERSION}_darwin_amd64.tar.gz"
  BINARY="cursor-macos"
elif [ "$OS" = "linux" ]; then
  ARCHIVE="cursor_v${VERSION}_linux_amd64.tar.gz"
  BINARY="cursor-linux"
fi

URL="https://github.com/$REPO/releases/download/v${VERSION}/$ARCHIVE"
TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

echo "downloading cursor v${VERSION} for ${OS}/${ARCH}..."
curl -fsSL "$URL" -o "$TMPDIR/$ARCHIVE"

echo "extracting..."
tar -xzf "$TMPDIR/$ARCHIVE" -C "$TMPDIR"

mkdir -p "$INSTALL_DIR"
cp "$TMPDIR/$BINARY" "$INSTALL_DIR/cursor-agent"
chmod +x "$INSTALL_DIR/cursor-agent"

echo "installed cursor-agent to $INSTALL_DIR/cursor-agent"
echo "run: cursor-agent --help"
