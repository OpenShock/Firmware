const fs = require('fs');
const ini = require('ini');
const semver = require('semver');
const core = require('@actions/core');

if (!fs.existsSync('VERSION')) {
  core.setFailed('File "VERSION" not found');
  process.exit();
}

if (!fs.existsSync('CHANGELOG.md')) {
  core.setFailed('File "CHANGELOG.md" not found');
  process.exit();
}

// Read files "VERSION" and "CHANGELOG.md"
let version = fs.readFileSync('VERSION', 'utf8').trim();
let changelog = fs.readFileSync('CHANGELOG.md', 'utf8').trim();
let platformioIniStr = fs.readFileSync('platformio.ini', 'utf8').trim();

// Validate that version is a valid semver (https://semver.org/#is-there-a-suggested-regular-expression-regex-to-check-a-semver-string)
if (semver.valid(version) === null) {
  core.setFailed(`File "VERSION" has invalid version "${version}"`);
  process.exit();
}

// Validate that changelog starts with "# Version <version>", then extract the first entry
const lines = changelog.split('\n');

if (!lines[0].startsWith('# Version ')) {
  core.setFailed('File "CHANGELOG.md" must start with "# Version <version>" followed by a changelog entry');
  process.exit();
}

if (!lines[0].startsWith(`# Version ${version}`)) {
  const changelogVersion = lines[0].substring(10).split(' ')[0];
  core.setFailed(`Mismatch between version in "VERSION" (${version}) and "CHANGELOG.md" (${changelogVersion}), you probably forgot to update both properly, please make sure they are both correct`);
  process.exit();
}

// Find the next line that starts with "# Version", if found, delimiter is the line before
let nextVersionLine = lines.findIndex((line, index) => index > 0 && line.startsWith('# Version '));
if (nextVersionLine !== -1) {
  changelog = lines.slice(0, nextVersionLine).join('\n').trim();
}

// Parse platformio.ini and extract the different boards
let platformioIni = ini.parse(platformioIniStr);

// Get every key that starts with "env:", and that isnt "env:fs" (which is the filesystem)
let boards = Object.keys(platformioIni)
  .filter((key) => key.startsWith('env:') && key !== 'env:fs')
  .reduce((arr, key) => {
    arr.push(key.substring(4));
    return arr;
  }, []);

// Set outputs
core.setOutput('version', version);
core.setOutput('changelog', changelog);
core.setOutput('board-list', boards.join('\n'));
core.setOutput('board-array', JSON.stringify(boards));
core.setOutput('board-matrix', JSON.stringify({ board: boards }));
