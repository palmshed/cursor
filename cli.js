#!/usr/bin/env node
const { spawn } = require('child_process');
const path = require('path');
const fs = require('fs');

const binaryDir = path.join(__dirname, 'binary');
const ext = process.platform === 'win32' ? '.exe' : '';
const binary = path.join(binaryDir, `cursor-agent${ext}`);

if (!fs.existsSync(binary)) {
  console.error('cursor binary not found. Run `npm install` to download it.');
  process.exit(1);
}

const proc = spawn(binary, process.argv.slice(2), { stdio: 'inherit' });
proc.on('exit', (code) => process.exit(code));
