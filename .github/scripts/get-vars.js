const fs = require('fs');
const ini = require('ini');
const semver = require('semver');
const core = require('@actions/core');
const child_process = require('child_process');

function getGitBranch() {
  // Get branch name
  const gitRef = process.env.GITHUB_REF;
  if (!gitRef) {
    core.setFailed('Environment variable "GITHUB_REF" not found');
    process.exit();
  }

  if (!gitRef.startsWith('refs/heads/')) {
    return null;
  }

  return gitRef.substring(11);
}
function getGitCommit() {
  try {
    return child_process.execSync('git rev-parse --short HEAD').toString().trim();
  } catch (error) {
    return 'unknown';
  }
}
function getVersionChangeLog(lines, version, isMaster) {
  const emptyChangelog = lines.length === 0;

  // Enforce that the changelog is not empty if we are on the master branch
  if (isMaster && emptyChangelog) {
    core.setFailed('File "CHANGELOG.md" is empty, this must be populated in the master branch');
    process.exit();
  }

  if (emptyChangelog) {
    return '';
  }

  // Simple validation of the changelog
  if (!lines[0].startsWith('# Version ')) {
    core.setFailed('File "CHANGELOG.md" must start with "# Version <version>" followed by a changelog entry');
    process.exit();
  }

  // Get the start of the entry
  const changeLogBegin = lines.findIndex((line) => line.startsWith(`# Version ${version}`));
  if (isMaster && changeLogBegin === -1) {
    core.setFailed(`File "CHANGELOG.md" does not contain a changelog entry for version "${version}", this must be added in the master branch`);
    process.exit();
  }

  // Enforce that the changelog entry is at the top of the file if we are on the master branch
  if (isMaster && changeLogBegin !== 0) {
    core.setFailed(`Changelog entry for version "${version}" is not at the top of the file, you tag is either out of date or you have not updated the changelog`);
    process.exit();
  }

  // Get the end of the entry
  let changeLogEnd = lines.slice(changeLogBegin + 1).findIndex((line) => line.startsWith('# Version '));
  if (changeLogEnd === -1) {
    changeLogEnd = lines.length;
  } else {
    changeLogEnd += changeLogBegin + 1;
  }

  const emptyChangelogEntry = lines.slice(changeLogBegin + 1, changeLogEnd).filter((line) => line.trim() !== '').length === 0;

  // Enforce that the changelog entry is not empty if we are on the master branch
  if (isMaster && emptyChangelogEntry) {
    core.setFailed(`Changelog entry for version "${version}" is empty, this must be populated in the master branch`);
    process.exit();
  }

  return lines.slice(changeLogBegin, changeLogEnd).join('\n');
}

const gitBranch = getGitBranch();
const isMaster = gitBranch === 'master';
const isDevelop = gitBranch === 'develop';

// Make sure we have all the files we need
for (const file of ['RELEASE.md', 'CHANGELOG.md', 'platformio.ini']) {
  if (!fs.existsSync(file)) {
    core.setFailed(`File "${file}" not found`);
    process.exit();
  }
}

// Read files "VERSION" and "CHANGELOG.md"
let release = fs.readFileSync('RELEASE.md', 'utf8');
let fullChangelog = fs.readFileSync('CHANGELOG.md', 'utf8').trim();
let platformioIniStr = fs.readFileSync('platformio.ini', 'utf8').trim();

// Pull history from git
child_process.execSync('git fetch --unshallow');

// Fetch latest git tag
let version = child_process.execSync('git describe --tags --abbrev=0').toString().trim();

// Parse and validate version
let parsedVersion = semver.parse(version);
if (parsedVersion === null || parsedVersion.loose) {
  core.setFailed(`Latest git tag "${version}" is not a valid semver version`);
  process.exit();
}

// Rebuild version string
version = `${parsedVersion.major}.${parsedVersion.minor}.${parsedVersion.patch}`;
if (!isMaster) {
  version += `-${gitBranch.replace(/[^a-z0-9]/gi, '.')}+${getGitCommit()}`;
}

const changelogLines = fullChangelog.split('\n');

// Get the changelog for the current version
const versionChangeLog = getVersionChangeLog(changelogLines, version, isMaster);

// Finish building the release string
release = versionChangeLog ? versionChangeLog + '\n\n' + release : release;

// Get all versions from the changelog
const changelogVersions = changelogLines.filter((line) => line.startsWith('# Version ')).map((line) => line.substring(10).split(' ')[0].trim());

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
core.setOutput('release', release);
core.setOutput('changelog', versionChangeLog);
core.setOutput('full-changelog', fullChangelog);
core.setOutput('board-list', boards.join('\n'));
core.setOutput('board-array', JSON.stringify(boards));
core.setOutput('board-matrix', JSON.stringify({ board: boards }));
core.setOutput('version-list', changelogVersions.join('\n'));
core.setOutput('version-array', JSON.stringify(changelogVersions));
