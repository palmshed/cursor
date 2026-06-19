const { execSync } = require('child_process');
const fs = require('fs');
const path = require('path');
const https = require('https');

const pkg = require('./package.json');
const version = pkg.version;
const binaryDir = path.join(__dirname, 'binary');

const arch = process.arch === 'x64' ? 'amd64' : process.arch;
const platformMap = {
  darwin: `cursor_v${version}_darwin_arm64.tar.gz`,
  linux: `cursor_v${version}_linux_amd64.tar.gz`,
  win32: `cursor_v${version}_windows_amd64.zip`,
};

const archive = platformMap[process.platform];
if (!archive) {
  console.error(`unsupported platform: ${process.platform}`);
  process.exit(1);
}

const url = `https://github.com/bniladridas/cursor/releases/download/v${version}/${archive}`;

fs.mkdirSync(binaryDir, { recursive: true });

console.log(`downloading cursor v${version}...`);

const file = fs.createWriteStream(path.join(binaryDir, archive));
function download(url) {
  https.get(url, (res) => {
    if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location) {
      download(res.headers.location);
      return;
    }
    if (res.statusCode !== 200) {
      console.error(`download failed (${res.statusCode}): ${url}`);
      process.exit(1);
    }
    res.pipe(file);
    file.on('finish', () => {
      file.close();
      console.log('extracting...');
      if (process.platform === 'win32') {
        execSync(`tar -xf "${path.join(binaryDir, archive)}" -C "${binaryDir}"`, { stdio: 'inherit' });
        const extracted = path.join(binaryDir, 'cursor-windows.exe');
        const target = path.join(binaryDir, 'cursor-agent.exe');
        fs.renameSync(extracted, target);
      } else {
        execSync(`tar -xzf "${path.join(binaryDir, archive)}" -C "${binaryDir}"`, { stdio: 'inherit' });
        const extracted = path.join(binaryDir, process.platform === 'darwin' ? 'cursor-macos' : 'cursor-linux');
        const target = path.join(binaryDir, 'cursor-agent');
        fs.renameSync(extracted, target);
      }
      fs.unlinkSync(path.join(binaryDir, archive));
      console.log('done');
    });
  }).on('error', (err) => {
    console.error(`download failed: ${err.message}`);
    process.exit(1);
  });
}
download(url);
