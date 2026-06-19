#!/usr/bin/env node
const { spawn } = require('child_process');
const path = require('path');
const fs = require('fs');

const binaryDir = path.join(__dirname, 'binary');
const ext = process.platform === 'win32' ? '.exe' : '';
const binary = path.join(binaryDir, `cursor-agent${ext}`);

function looksLikeBinary(filePath) {
  try {
    const stat = fs.statSync(filePath);
    if (stat.size < 50000) return false;
    const fd = fs.openSync(filePath, 'r');
    const buf = Buffer.alloc(4);
    fs.readSync(fd, buf, 0, 4, 0);
    fs.closeSync(fd);
    return buf[0] !== 0x3c && buf[0] !== 0x7b && buf[0] !== 0x23;
  } catch {
    return false;
  }
}

function validateBinary() {
  if (!fs.existsSync(binary)) {
    console.error('cursor binary not found. Run `npm install` to download it.');
    process.exit(1);
  }
  if (!looksLikeBinary(binary)) {
    console.error('cursor binary appears corrupted (downloaded an HTML/text file instead of an executable).');
    console.error('Restore a working binary, or run `npm install` to reinstall.');
    process.exit(1);
  }
}

validateBinary();

const proc = spawn(binary, process.argv.slice(2), { stdio: 'inherit' });
proc.on('exit', (code) => process.exit(code));
