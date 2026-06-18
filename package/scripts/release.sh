#!/bin/bash
# Cursor Agent - Release Script

set -e

VERSION=$(cat ../../VERSION | tr -d '\n')
RELEASE_DIR="release/v${VERSION}"

echo "Building release v${VERSION}..."

make clean && make build

mkdir -p "$RELEASE_DIR"
make package
cp -r package/dist/* "$RELEASE_DIR/"
git archive --format=tar.gz --prefix="cursor-agent-${VERSION}/" HEAD > "$RELEASE_DIR/cursor-agent-${VERSION}-source.tar.gz"

cd "$RELEASE_DIR"
shasum -a 256 * > checksums.sha256

echo "Release ready: $RELEASE_DIR"
ls -la
